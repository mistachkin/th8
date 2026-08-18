# TH8: A Secure Embeddable Tcl for the Age of Agentic AI

**Joe Mistachkin**

*Prepared for the Tcl/Tk Conference 2026*

---

## Abstract

TH8 is a from-scratch implementation of the Tcl scripting language
designed for secure embedding in host applications and for safe
execution of scripts in environments where untrusted or
AI-generated code is a first-class concern.  Unlike traditional Tcl
interpreters, TH8 has *no inherent capabilities*: every interaction
with the outside world --- file I/O, memory allocation, network
access, binary loading --- is routed through an explicit platform
abstraction layer that the embedder composes at initialization time.

This paper makes four arguments.  First, that Tcl needs a formal
language standard, and that the security properties required by
modern embedding scenarios cannot be retrofitted onto an existing
implementation --- they must be structural.  Second, that a
tree-walking interpreter with an internal-representation cache can
deliver adequate performance for embedding use cases without a
bytecode compiler, and that the absence of bytecode is itself a
security property.  Third, that TH8's zero-capability architecture
is fundamentally different from what Tcl extensions or safe
interpreters can provide, because the security boundary is at the
platform linkage level, not the command-hiding level.  Fourth, that
large language models are effective collaborators for building
formally specified, security-critical systems software --- TH8 was
developed in sustained collaboration with an LLM, and this paper
describes the methodology, its strengths, and its limitations.

The paper covers TH8's architecture, the Tcl Language Standard v1
plus its companion specifications (1,151 normative requirements
across the standard, the C API spec, and the language extensions;
three conformance profiles), the defense-in-depth security model,
compile-time modularity (16 independently gateable plugins plus
the `crypto` and `regexp` subdirectory plugins), integration
patterns for agentic
AI systems, and the use of TH8 as a memory representation language
for large language models.

---

## 1.  Introduction

The rise of agentic AI systems --- large language models (LLMs) that
take actions, write code, and execute tools on behalf of users ---
has created a pressing need for scripting runtimes that are *safe by
construction*.  When an LLM generates a script and requests its
execution, the host application must guarantee that the script
cannot escape its sandbox, exfiltrate data, corrupt state, or
consume unbounded resources.

Tcl's original "everything is a string" philosophy, its simple
syntax, and its long history of embedding (Expect, Fossil's TH1,
AOLserver, countless EDA tools) make it an attractive choice for
this role.  But standard Tcl interpreters expose a large,
capability-rich surface by default: file I/O, socket operations,
`exec`, `load`, and direct access to the file system.  Removing
these capabilities after the fact (via safe interpreters or
interp-alias hiding) is fragile and has a history of bypasses.

TH8 takes the opposite approach.  A freshly created interpreter
can do *nothing* except evaluate pure computation.  Capabilities
are added explicitly by the embedder through a composable platform
layer.  The result is a Tcl implementation where the security
boundary is architectural rather than administrative.

### 1.1  Heritage

TH8 descends from TH1, the minimal Tcl interpreter embedded in the
Fossil distributed version control system.  TH1 demonstrated that a
stripped-down Tcl could serve as a safe configuration and templating
language inside a security-conscious application.  TH8 generalizes
that idea: it is a full-fidelity Tcl 8.4 implementation (with
selected modern extensions) that retains TH1's zero-capability
starting point while adding the language features, formal
specification, and security infrastructure needed for production
use.

The standard is dedicated to the memory of Miguel Sofer
(d. 2016), whose work on the Non-Recursive Engine, coroutines, and
the variable system in Tcl 8.6 demonstrated that an interpreter's
deepest architecture could be reimagined without sacrificing
compatibility.

### 1.2  Contributions

This paper makes the following contributions:

1.  **Formal specification.**  The Tcl Language Standard v1
    (and its companion specifications) carry 1,151 normative
    requirements, three conformance profiles, and an MD5-based
    requirement marking system traceable to tests.

2.  **Zero-capability architecture.**  A platform abstraction layer
    with composable capability profiles (Core, Standard, Full) that
    gives the embedder complete control over the interpreter's
    external surface.

3.  **Cryptographic script policy.**  A signed-only execution mode
    using RSA-SHA512 Authenticode-style signatures, embedded
    public keys, and a three-tier pre-evaluation gate.

4.  **Agentic AI integration model.**  A description of how TH8's
    architecture maps onto the requirements of LLM tool-use,
    code-generation, and agent-orchestration scenarios.

5.  **LLM-assisted development methodology.**  A candid account of
    building a formally specified, security-critical C codebase in
    sustained collaboration with a large language model, including
    where the approach excels and where human judgment remains
    irreplaceable.


---

## 2.  Language and Specification

### 2.1  Scope

TH8 implements the Tcl 8.4 surface syntax.  Extensions beyond
Tcl 8.4 are limited to:

-   Non-recursive evaluation (NRE) with coroutine and yield support
-   Tail call optimization (`tailcall`)
-   The `**` exponentiation operator
-   Strict UTF-8 string semantics
-   Additional procedure forms (`nproc`, `napply`)
-   `subst` with TIP #712 semantics
-   C99 and IEEE 754 math functions (60 registered functions)
-   Ephemeral frame semantics for `namespace eval` (see below)

One intentional deviation from Tcl 8.x/9.x: `set` inside
`namespace eval` creates ephemeral frame-local variables that are
destroyed when the eval completes.  Only the `variable` command
creates persistent namespace state.  This separates scratch
variables from declared state, prevents namespace pollution, and
makes the `variable` command the single, self-documenting mechanism
for persistent namespace variables.

The specification defines *observable behavior only*.  It does not
prescribe implementation strategies, data structures, or algorithms
except where the observable behavior of list formatting requires a
specific canonical representation.

### 2.2  Normative Requirements

Every normative statement in the standard is tagged with a
deterministic requirement identifier of the form **R-*nnnnn*-*nnnnn***,
computed by hashing the normalized requirement text with MD5:

```
R-29508-16704
:   The maximum byte length of any string value is
    100 MB (104,857,600 bytes).
```

The `tools/mkreq.tcl` script generates, verifies, and refreshes
these identifiers.  The algorithm is identical in principle to the
requirement marking system used by the SQLite project, providing
a stable, content-addressed link between specification text and
test cases.

As of this writing (August 2026), the standard and its companion
specifications (Tcl Language Standard v1, TH8 Public C API
Specification, Tcl Language Extensions) contain **1,151
normative requirements** across 41 sections.  The TH8 test
suite provides coverage for **98.52%** of these requirements
(1,134 of 1,151) across 2,511 conformance test invocations in
265 test files under `tests/`, with
`mkreq.tcl --check-tests` reporting zero orphan markers.  The remaining 17 uncovered requirements
are concentrated in platform / C-API, crypto / security, and
debug-API sections that require specialized C-level test
infrastructure not yet built out.

### 2.3  Conformance Profiles

The standard defines three conformance profiles, each a strict
superset of the one below it:

| Profile | Commands | Target |
|---------|----------|--------|
| **Core** | ~45 | Configuration, glue, sandboxed embedding |
| **Standard** | ~75 | Extension scripting, build tools, automation |
| **Full** | ~90+ | Standalone interpreter, reference implementation |

A Core-profile interpreter can evaluate scripts that compute
values, transform data, and make decisions --- but cannot
interact with the file system, load extensions, or introspect
deeply into the interpreter state.  This is the profile most
relevant to agentic AI use cases.


---

## 3.  Architecture

### 3.1  Platform Abstraction

The central design decision in TH8 is the `Th8_Platform`
structure: a version-3 table of 66 function pointers that mediate
every interaction between the interpreter and its environment.  A
freshly created interpreter with a NULL platform can evaluate
pure expressions and manipulate strings, but cannot allocate
memory, read files, or produce output.

TH8 ships eleven composable platform layers:

| Layer | Capabilities |
|-------|-------------|
| `Th8_GetLibcPlatform()` | C runtime: memory, byte ops, math, formatting |
| `Th8_GetNullIoPlatform()` | Safe null I/O: no side effects |
| `Th8_GetPosixPlatform()` | Full POSIX: files, dlopen, signals, threads |
| `Th8_GetWin32Platform()` | Full Windows: Win32 API, LoadLibrary, console |
| `Th8_GetMacOSPlatform()` | macOS: private malloc zone for isolation |
| `Th8_GetCurlPlatform()` | libcurl: HTTPS data retrieval with TLS |
| `Th8_GetMimallocPlatform()` | mimalloc: high-performance allocation (1.7-2x faster) |
| `Th8_GetCosmopolitanPlatform()` | Cosmopolitan Libc: single binary for 6 OSes |
| `Th8_GetEnvPlatform()` | Environment variable access control |
| `Th8_GetMemPlatform()` | Memory recovery: IR cache clearing + retry on OOM |
| Fault injection (via `Th8_FaultInstall`) | Deterministic allocation failure for testing |

Layers are composed via `Th8_MergePlatform()`, which fills NULL
slots in a destination platform from a source.  This is additive:
once a capability is set, it cannot be overwritten by a merge.
The embedder constructs the exact capability set needed:

```c
/* Maximum-security sandbox: memory + math, nothing else. */
Th8_Platform *p = Th8_ClonePlatform(Th8_GetLibcPlatform());
Th8_MergePlatform(p, Th8_GetNullIoPlatform());
Th8_Initialize(p);
Th8_Interp *interp = Th8_CreateInterp(p);
```

```c
/* Full development shell. */
Th8_Platform *p = Th8_ClonePlatform(Th8_GetPosixPlatform());
Th8_MergePlatform(p, Th8_GetLibcPlatform());
Th8_MergePlatform(p, Th8_GetCurlPlatform());
Th8_Initialize(p);
Th8_Interp *interp = Th8_CreateInterp(p);
```

Because the platform is an explicit parameter to interpreter
creation, the security boundary is *structural*: there is no
`interp eval {file delete /}` to worry about, because the
interpreter literally has no file-deletion callback.

The platform struct (now at version 3) organizes its 66 callbacks
into 16 logical groups --- lifecycle, memory, byte operations,
string/utility, threading, I/O core, I/O redirection,
channel/temporary I/O, filesystem, data/loading, time,
process/host, error/diagnostics, math/entropy, and host context.
As a consequence of this thorough abstraction, the
platform-neutral core is entirely CRT-free: every C runtime call
(`memcpy`, `strcmp`, `qsort`, `snprintf`, etc.) is routed through
the platform layer, so the interpreter itself links against no
system library.

### 3.2  Non-Recursive Evaluation

TH8 uses a trampoline-driven Non-Recursive Engine (NRE) inspired
by Miguel Sofer's work in Tcl 8.6.  Script evaluation pushes
callbacks onto an interpreter-local chain rather than recursing
on the C stack.  The outermost `Th8_Eval` drains the callback
chain in a loop, eliminating C stack overflow for deeply nested
scripts.

This design also enables coroutines (`coroutine`/`yield`),
suspension (`Th8_Suspend`/`Th8_Thaw`), and asynchronous
cancellation (`interp cancel`) --- all critical for agentic use
cases where a host may need to preempt a long-running script.

### 3.3  Resource Limits

Independent of the platform layer, TH8 enforces configurable
resource limits:

-   **Step counter** --- bounds counted work-units (command and
    loop-iteration checkpoints), not literally every CPU instruction:
    a few inner loops are still being brought under the poll, so the
    counter is a cooperative progress bound, not a hard cycle cap.
    The host can set a limit and check progress.
-   **Result size limit** --- bounds the size of any string value
    the interpreter can construct.
-   **Memory allocation limit** --- bounds the requested bytes across
    allocations with bounded slack (it caps what the interpreter asks
    for, not the exact resident set the allocator holds).
-   **Stack bounds** --- the NRE engine queries native stack bounds
    via the platform and refuses to recurse beyond a safe margin.
-   **Integer overflow checking** --- optional strict mode that
    treats arithmetic overflow as an error.

These limits compose with the platform abstraction: a sandboxed
interpreter has both *no capabilities* and *bounded resources*.

### 3.4  Graceful Allocation Failure

In a conventional Tcl implementation, failed memory allocation is
fatal: `ckalloc` panics and aborts the process.  This is
acceptable for a standalone interpreter but unacceptable for an
embedded one --- a failed allocation in a sandboxed child
interpreter must not crash the host application.

TH8 addresses this with a dual allocation API modeled on Tcl's
`attemptckalloc` pattern.  `Th8_Malloc` and `Th8_Realloc` panic on
failure (suitable for core interpreter infrastructure that cannot
proceed without memory).  `Th8_AttemptMalloc` and
`Th8_AttemptRealloc` return NULL on failure, allowing the caller to
report an out-of-memory error via the interpreter result and unwind
gracefully.  A companion function, `Th8_SetResultStatic`, sets the
interpreter result to a static string literal without allocating
memory, ensuring that out-of-memory error paths do not themselves
require allocation.

The AttemptMalloc conversion was systematic: every call site
reachable from script evaluation was audited and converted, while
internal infrastructure paths (hash table growth, interpreter
creation) retained the panicking allocator.  The result is that a
memory-limited sandbox interpreter can exhaust its allocation budget
and return a clean `TH8_ERROR` to the host without terminating the
process.

### 3.5  Simplified Embedder Setup

While the composable platform layer provides maximum flexibility,
many embedders want a single-call setup that provides sensible
defaults.  `Th8_UseDefaultPlatform` populates a caller-supplied
`Th8_Platform` struct with the standard configuration for the
current operating system: libc for memory and math, the
OS-appropriate platform (POSIX, Win32, or macOS) for I/O and
threading, and null-I/O as a fallback for any remaining slots.
The typical embedder initialization reduces to:

```c
Th8_Platform platform;
memset(&platform, 0, sizeof(platform));
Th8_UseDefaultPlatform(&platform);
Th8_Initialize(&platform);
Th8_Interp *interp = Th8_CreateInterp(&platform);
```

This preserves the zero-capability principle --- the embedder still
controls the platform struct and can NULL out specific callbacks
after the default fill --- while eliminating the boilerplate of
manual layer composition for the common case.

### 3.6  Unified Policy Callback

Early versions of TH8's signed-only policy used two separate
callbacks: `preGetData` (fired inside `Th8_GetData` after raw bytes
were read) and `preEval` (fired at the top of every evaluation).
These were consolidated into a single `Th8_PolicyProc` callback
invoked at four distinct phases, identified by a bitmask combining a
timing bit (`TH8_PHASE_PRE` or `TH8_PHASE_POST`) with an operation
bit (`TH8_PHASE_EVAL` or `TH8_PHASE_READ`).  The callback receives
the phase, the script name, the script data, and a user context
pointer.

The unification simplifies the embedder API (one setter,
`Th8_SetPolicyCallback`, instead of two), ensures that pre-read and
pre-eval policy decisions share the same state (critical for the
verification-flag handoff described in Section 4.3), and makes it
straightforward to add new phases in the future without adding new
callback slots.

### 3.7  Performance Without Bytecode

A common objection to tree-walking interpreters is performance.
TH8 addresses this through two mechanisms.

**Internal-representation cache.**  TH8 maintains a per-interpreter
hash table that caches string-to-value conversions.  When a string
is first interpreted as an integer, double, boolean, list, dict,
bigint, or command name, the converted value is stored in the
cache.  Subsequent uses of the same string skip the parse entirely.
This eliminates the primary cost that bytecode compilation avoids
--- repeated re-parsing of operands --- without introducing a
second representation that must be kept synchronized with the
canonical string form.

The cache supports seven value types:

| Type | Cached form | Avoids |
|------|-------------|--------|
| Integer | `int` | `atoi` / hex / octal / binary parse |
| Wide | `th8_int64_t` | 64-bit integer parse |
| Double | `double` | Full floating-point parse |
| Boolean | `int` (0/1) | String comparison against true/false/yes/no/on/off |
| List | Split element array | List splitting (the most expensive common operation) |
| Dict | Key-value pair array | Dict splitting and key lookup |
| Bigint | `mp_int` | Arbitrary-precision parse |
| Command | `Th8_Command *` | Hash table lookup in command resolution |

The cache is invalidated when a string value changes (via `set`,
`append`, etc.), preserving the "everything is a string" invariant.

**No compilation latency.**  TH8 interpreters start in microseconds.
There is no bytecode compilation pass, no JIT warmup, no code-cache
warming.  For embedding use cases --- configuration evaluation,
agentic tool calls, per-request script execution --- startup
latency dominates throughput.  A system that creates and destroys
interpreters per-request (Pattern 4 in Section 5) benefits more
from instant start than from steady-state throughput optimization.

**Security argument.**  The absence of bytecode is also a
*security* property.  Bytecode is a second representation of the
program that must be kept in perfect synchronization with the
source string.  TH8's "everything is a string" invariant means
that the source text IS the program --- there is no hidden compiled
form that could diverge from the inspectable text.  The preEval
callback, the signed-only policy, and the script origin tracking
all operate on the string that will be parsed.  If bytecode existed,
these mechanisms would need to verify the bytecode as well, opening
a class of TOCTOU (time-of-check/time-of-use) vulnerabilities.

### 3.8  Compile-Time Modularity

TH8's ~90 built-in commands are organized into 17 independently
gateable plugins, each controlled by a `TH8_PLUGIN_*` compile-time
define.  Three core subsystems (expressions, dynamic loading,
variables) have their own `TH8_ENABLE_*` gates with automatic
dependency cascading:

-   `TH8_ENABLE_EXPRESSIONS=0` disables the expression parser and
    automatically disables the control, expressions, formatting,
    and looping plugins.
-   `TH8_ENABLE_VARIABLES=0` eliminates the entire variable system;
    `$`-substitution returns an error.  Zero-parameter procedures
    still work, creating a "purely functional" language mode.
-   `TH8_ENABLE_LOAD=0` disables binary loading and automatically
    disables the extensibility plugin.

The minimal build (all optional features disabled) produces a 508 KB
static library --- a 27% reduction from the 697 KB full build.
This modularity serves both the standard's three conformance
profiles (Core requires fewer plugins than Standard) and
embedded deployments where code size is constrained.

Because these flags are public and supported, every combination of
their values is part of the contract, so each feature's *use*-sites
are gated on exactly the feature(s) they need --- a conjunction where
a site depends on two (secure variables, needing both cryptography and
the variable system, are `#if CRYPTOGRAPHY && VARIABLES`), never a
coarse block that conditions a two-feature site on one flag.  A
build-matrix sweep exercises each flag off in isolation and, as its
strongest single check, the all-features-off build --- the one
configuration in which every per-site guard must hold at once ---
turning "the minimal configuration links and runs" from a documented
aspiration into an enforced test (Section 6.4, Lesson 22).

### 3.9  Deferred Deletion (Pending-Delete Queue)

When a command deletes itself during dispatch (e.g., a coroutine
whose body completes and auto-deletes its resume command) or when
a namespace is deleted from within one of its own commands, the
immediate destruction would free memory still referenced by the
call stack.  TH8 solves this with a FIFO pending-delete queue:
the command or namespace is removed from the hash table
immediately (preventing further invocation), but the destructor
callback and memory free are deferred until the eval stack fully
unwinds.  The queue is drained whenever `nEvalDepth` drops to
zero.  This eliminates an entire class of use-after-free
vulnerabilities inherent in languages that allow self-modifying
code.

### 3.10  SplitList Cache Bypass

`Th8_SplitList` caches parsed list representations in the IR
(internal-representation) cache for amortized O(1) repeated
splits of the same string.  However, when the input buffer is
temporary (e.g., a key list built by `ListAppend` inside a
platform callback), caching it would leave dangling element
pointers after the buffer is freed.  The `TH8_LIST_NO_CACHE`
flag, added to all 62 `Th8_SplitList` call sites, allows
callers to bypass the cache for temporary buffers while
retaining the performance benefit for stable strings.

---

## 4.  Security Model

### 4.1  Threat Model

TH8 protects against three concrete adversaries: an **untrusted
script** (hostile or compromised code presented to the
interpreter), **untrusted data** (attacker-controlled input
processed by an otherwise-benign script), and a **local
unprivileged observer** (another process on the same host
attempting to read TH8's memory or persistent state).

Three classes of adversary are explicitly *out of scope*: a
**privileged local user** (root, kernel, or anything with
ptrace/kernel-module access), **hardware-level attackers**
(cold-boot DRAM, DMA, side-channels), and **same-process code**
(a malicious plugin, compromised CRT, or any code sharing TH8's
address space).  TH8's `mlock`'d protected memory deters accidental
swap or core-dump exposure; it does not pretend to defend against
an attacker who has root or kernel privileges on the same machine.
This honesty is itself a security property: stating the limits
prevents embedders from relying on TH8 to do work it cannot do.

A non-obvious in-scope threat is worth highlighting separately:
**a script that exploits a yet-undiscovered bug in the interpreter
itself** to obtain an arbitrary-memory-read primitive.  TH8's
defense against this is encryption-at-rest of in-memory secrets:
secure-variable values exist as AES-256-GCM ciphertext in heap
for almost all of their in-memory lifetime, with plaintext only
briefly materialized in protected memory during read/save
operations.  A script that achieved arbitrary-read would receive
ciphertext, not plaintext --- exfiltrating a single secret would
require additional independent reads of the per-variable key, the
GCM nonce, and the tag (located in disjoint regions of memory),
plus a decryption oracle.  The blast radius of any single exploit
primitive is therefore bounded.  This treats TH8's own process
memory as untrusted with respect to the script being executed
--- a deliberate stance rather than an accidental one.

The full threat model --- 31 in-scope threats, 8 out-of-scope
adversary classes, an attack-surface inventory, and a feature-to-
threat mapping --- is documented in `security_model.md`,
Section 0.  The remainder of this section summarizes the principal
defensive mechanisms.

### 4.2  Defense in Depth

TH8's security model comprises 34 interlocking layers, ranging
from the platform abstraction (the primary boundary) through
compile-time hardening, hash table DoS resistance, and fuzz
testing infrastructure.  Selected highlights:

-   **Random-token security gates.**  Binary loading, the
    signed-only policy, and the verification flag all use 64-bit
    random tokens.  A capability is enabled only when two
    independently stored tokens match.  Single-bit corruption
    (e.g., from a memory safety bug) cannot activate a gate.

-   **SipHash-2-4 hash tables.**  All hash tables use SipHash
    with per-process random keys, preventing algorithmic
    complexity attacks via crafted key collisions.

-   **System variables.**  The `::th8_security` array is
    declared as a system variable and cannot be modified from
    scripts.  The array reflects the cryptographic verification
    state of the currently executing script.

-   **Script origin tracking.**  Every call to `Th8_Eval` carries
    an origin name (the source file path or URI) that is threaded
    through the entire evaluation chain.  This provenance is used
    by the signed-only policy to verify that every script has a
    companion signature file.

-   **OpenBSD pledge/unveil.**  The interactive shell restricts
    itself to the minimum system call set and filesystem paths
    after initialization.

### 4.3  Signed-Only Script Policy

TH8's most distinctive security feature is its *signed-only
execution mode*.  When enabled, every script must have a companion
`.b64sig` Harpy signature file containing an RSA-SHA512 signature
over the raw script bytes.

The policy operates at three layers:

1.  **preGetData callback** --- fires inside `Th8_GetData` after
    raw file bytes are read but before line-ending translation.
    Reads the `.b64sig` file, parses the Harpy header to extract
    the public key token, fetches (or uses cached) the
    corresponding RSA public key, and verifies the signature.

2.  **preEval callback** --- fires at the top of every
    `th8EvalLocal` before parsing begins.  A three-tier gate:
    -   If signed-only is disabled (via save/restore), allow.
    -   If the verification flag is set (from preGetData), allow.
    -   If eval depth > 1 (internal sub-eval like `if`, `catch`),
        allow.
    -   Otherwise reject.

3.  **Th8_EvalFileAsData** --- creates an isolated child
    interpreter for loading additional RSA keys through signed
    key-delivery scripts.  The child inherits the parent's policy.

Embedded public keys (compiled into the library) establish a
trust anchor independent of any external key distribution.
Additional keys can be fetched from a well-known registry URL and
cached per-interpreter.

The signing infrastructure is designed for post-quantum migration:
the Harpy `.b64sig` format is algorithm-agnostic, key tokens are
algorithm-independent, and the RSA implementation is isolated in
two files (`th8_snk.c`, `th8_policy.c`).  RSA keys up to 131072
bits are supported.  The embedded 16384-bit root key provides a
conservative safety margin through ~2060 under aggressive quantum
improvement assumptions, buying time for migration to NIST PQC
algorithms (CRYSTALS-Dilithium, SPHINCS+).

### 4.4  Trusted Evaluation and Scoped Authorization

The signed-only policy creates a tension for interactive use: when
a user types a command at the TH8 shell prompt, there is no
signature file.  The command is legitimate --- the user is present
at the keyboard --- but the preEval gate would reject it.
`Th8_EvalTrusted` resolves this by providing an explicit trust
assertion from the embedder.  It is identical to `Th8_Eval` except
that the interpreter's verification flag is set before evaluation
begins, causing the preEval policy gate to allow the script through.
Only named script origins (file paths) go through the normal
signature verification path; the trust assertion is reserved for
cases where the embedder --- not a file --- vouches for the script.

Authorization is scoped: the `POST|EVAL` phase of the unified
policy callback clears the verification flag after each evaluation
completes.  This ensures that a trusted evaluation does not leave
the interpreter in a permanently trusted state.  Every subsequent
`Th8_Eval` (including internal sub-evaluations like `source` called
from within a trusted script) must re-establish trust through either
a valid signature or an explicit `Th8_EvalTrusted` call.  The
verification flag is a per-evaluation assertion, not a persistent
mode.

### 4.5  Sandbox Child Interpreter Hardening

When TH8 creates a child interpreter for sandboxed evaluation
(e.g., `Th8_EvalFileAsData` for key-delivery scripts), the child
is configured with multiple interlocking restrictions.  The child's
`xPanic` callback is set to NULL, so a failed allocation in the
child cannot invoke the host's panic handler --- allocation failures
return NULL and propagate as script errors.  Memory and step limits
are set to conservative values independent of the parent's limits.
Binary loading is disabled (`TH8_ENABLE_LOAD=0` equivalent at
runtime).  The child inherits the parent's signed-only policy
callback but not its verification flag, so every script the child
evaluates must independently satisfy the policy.

This defense-in-depth approach ensures that even if a child
interpreter evaluates a malicious script, the damage is contained:
no host crash, no unbounded resource consumption, no native code
loading, and no trust leakage from the parent's verification state.

### 4.6  Environment Variable Access Control

Environment variables are a common vector for information
exfiltration and configuration injection.  TH8 routes all
environment variable access through the platform's `xGetEnv`
callback rather than calling `getenv()` directly.  If the platform
does not provide `xGetEnv` (i.e., the slot is NULL), environment
variable queries return NULL --- the interpreter cannot observe the
host's environment.  This is the default for sandboxed platform
configurations.

When `xGetEnv` is provided, the platform implementation can filter,
audit, or virtualize environment variables.  A security-conscious
embedder might provide an `xGetEnv` that returns only an explicitly
allowed subset of variables, or one that returns synthetic values
for testing purposes.  The callback receives the interpreter pointer,
allowing per-interpreter access policies.

### 4.7  Remote Time Verification

The NTP and HTTPS time sources (Section 4.11) serve a specific
security function beyond clock synchronization: they provide a
trusted time reference for key
expiration and annotation timestamp checks.  Before verifying
`<<notBefore:...>>` and `<<notAfter:...>>` annotations in signed
scripts, TH8 can query a remote time source to detect local clock
manipulation.  An attacker who sets the system clock backward to
reactivate an expired key, or forward to bypass a not-yet-valid
annotation, is detected by the disagreement between the local clock
and the authenticated remote time.

The HTTPS time source is especially robust: the response includes a
random nonce (anti-replay) and an RSA-SHA512 signature from the
embedded `keyTime` public key (anti-forgery).  An attacker would
need to compromise both the local clock and the remote time
server's signing key to bypass the check.

### 4.8  Script Annotations

Signed scripts may embed `<<notBefore:YYYY_MM_DDThh_mm_ssZ>>`
and `<<notAfter:YYYY_MM_DDThh_mm_ssZ>>` annotations in Tcl
comments (lines beginning with `# <<`).  These provide temporal
access control: a script can be made valid only within a
specific time window.  The timestamp format uses underscores
instead of hyphens/colons for Eagle compatibility, and the parser
validates calendar semantics (leap years, day-of-month bounds,
and the UTC leap second at :60).

When annotations are present, the `::th8_security` array is
populated with seven elements: `algorithmName`, `dataName`,
`flags`, `notAfter`, `notBefore`, `policy`, and `publicKeyToken`.
The `Th8_EvalFile` function manages a save/restore stack so
that nested `source` calls preserve the outer file's security
identity while making annotation values visible to the caller.

### 4.9  Win32 Authenticode Enforcement

On non-debug Win32 builds, the DLL loader verifies the Authenticode
digital signature of every binary before `LoadLibraryA` is called.
This uses `WinVerifyTrust` with the strictest offline-compatible
settings: whole-chain revocation checking against locally cached
CRLs, the SAFER trust flag, and lifetime signing acceptance.
Unsigned or tampered DLLs are never mapped into the process.

### 4.10  Secure Variables

TH8 provides encrypted-at-rest variable storage for sensitive
values such as passwords, API tokens, and cryptographic material.
The `[secure]` command creates variables whose values are
encrypted with AES-256-GCM; encryption keys are held in a
non-pageable (`mlock`'d) memory page flanked by guard pages.

Key design decisions:

-   **Key rotation on every write** --- each `[set]` generates a
    fresh 256-bit key, limiting the window for key extraction.
-   **PKCS#7 padding to 64-byte blocks** --- provides length
    confidentiality (an observer cannot distinguish a 3-byte
    value from a 60-byte one).
-   **Variable name as GCM AAD** --- the ciphertext is bound to the
    specific variable; copying encrypted bytes between variables
    fails authentication.
-   **Canary integrity checking** --- an 8-byte magic value at the
    start of the key page is verified on every encrypt/decrypt.
-   **`madvise(MADV_DONTDUMP | MADV_WIPEONFORK)`** on Linux ---
    excludes key material from core dumps and zeroes it on fork.
-   **Sensitive result marking** --- the interpreter result buffer
    is securely zeroed when overwritten after a secure variable
    read.
-   **Hardened persistence backend** --- the SQLite key-value
    backend that holds master-encrypted secure-variable blobs is
    itself hardened: `SQLITE_OPEN_NOFOLLOW` against symlink
    attacks; `SQLITE_DBCONFIG_DEFENSIVE` plus `trusted_schema =
    OFF` plus a restrictive `sqlite3_set_authorizer` callback
    that turns the connection into a single-purpose KV endpoint
    (no ATTACH, no triggers, no schema mutation, no reads of
    other tables); a 25-step security-first pragma sequence
    (integrity_check first, schema creation last); a
    cancellation-aware busy handler that honors `Th8_Ready` so
    Ctrl-C reaches script level even during contention; and
    `secure_delete = ON` so deleted rows do not linger as
    recoverable bytes.  See `security_model.md` Section 22 for
    the full catalog.

### 4.11  Authenticated Time Verification

TH8 includes two network time sources for verifying key expiration
dates and detecting clock manipulation:

-   **NTP v4** (`clock ntp`) --- UDP-based, with multi-server
    consensus, anti-spoof origin timestamp checking, and
    configurable disagreement threshold.
-   **HTTPS time** (`clock https`) --- fetches from a signed time
    endpoint, verifying a random nonce (anti-replay) and an
    RSA-SHA512 signature (anti-forgery) against the embedded
    `keyTime` public key.

Both sources share a per-interpreter backward-clock detection
cache that raises an error if the local clock moves backward
relative to a prior verified reading.

### 4.12  Fuzz Testing

Six coverage-guided fuzz harnesses target the interpreter's primary
attack surfaces: script evaluation, expression parsing, list
parsing, format string processing, Harpy signature parsing, and
SNK/CAPI key blob parsing.  The harnesses support three execution
modes (standalone, LLVM libFuzzer, AFL++) and run under
AddressSanitizer and UndefinedBehaviorSanitizer.  Deterministic
execution (fixed PRNG seed, no `dlopen`) ensures stable coverage
maps for feedback-driven fuzzing.

Crash inputs from fuzzing campaigns are preserved as regression
tests.  The `tests/fuzzing.tcl` test file auto-discovers `.bin`
files under `tests/fuzzing/<subType>/` and replays each through
`th8testlib::fuzz`, which feeds raw data into the target subsystem
(list, eval, or expr) and verifies the interpreter survives
without crashing.  As of this writing, the list parser corpus
contains 24 crash inputs from an AFL++ campaign that exposed a
heap-buffer-overflow in the Spilornis backslash parser (see
Section 6.5).


### 4.13  Coverage Hardening: NEVER / ALWAYS and the Fuzz Synergy

TH8 inherits the SQLite convention of marking conditions believed
unreachable in production with two macros:

```c
ALWAYS(X)   /* X is expected to always be true  */
NEVER(X)    /* X is expected to always be false */
```

These wraps document an invariant — typically a defensive guard
whose negative branch is dead by construction (e.g. `!interp` when
the public-API perimeter has already validated `interp`).  Because
every wrap is a *claim* the developer asserts about the program,
the macros expand differently per build flavour:

* **Production** (`ALWAYS(X) → X`, `NEVER(X) → X`): the runtime check
  is preserved.  Cost is a single branch per site; safety is
  whatever the original condition gave you.
* **Coverage** (`-DTH8_OMIT_AUXILIARY_SAFETY_CHECKS`):
  `ALWAYS(X) → 1`, `NEVER(X) → 0`.  The conditions constant-fold,
  llvm-cov reports the corresponding MC/DC pairs as
  `constant folded` and excludes them from the denominator.  The
  measured coverage percentage then reflects only conditions the
  developer actually expects to be observable.
* **Debug** (`-DTH8_DEBUG`): `ALWAYS(X) → ((X)?1:(assert(0),0))`,
  `NEVER(X) → ((X)?(assert(0),1):0)`.  The wrap becomes an active
  trap.  If any execution ever evaluates an `ALWAYS`-wrapped
  condition to false, or a `NEVER`-wrapped condition to true, the
  process aborts at the offending site.

The third expansion is what makes the wraps load-bearing rather
than cosmetic.  An `ALWAYS`/`NEVER` wrap is only honest if the
codebase never violates it; debug builds turn that honesty into a
runtime check.  But a hand-audit cannot know whether a particular
guard is genuinely unreachable or merely unreached by the existing
test corpus.  The fuzz harnesses (Section 4.12) close that gap.

The interaction is straightforward.  When a TH8 binary is built
with `-DTH8_DEBUG -fsanitize=address,undefined` and run under
libFuzzer or AFL++ against any of the six harnesses, a triggered
`assert(0)` from inside a `NEVER` or `ALWAYS` wrap is treated by
the fuzzer's runtime as a crash: the input is minimised and saved
to the corpus.  That input is, by construction, a witness that the
wrap's invariant was wrong.  The remediation cycle is:

1.  **Capture.**  The minimised input is preserved as a regression
    test under `tests/fuzzing/<subType>/` and replayed by
    `tests/fuzzing.tcl` from then on, exactly as crash corpora are
    today.
2.  **Remove the wrap.**  The `NEVER`/`ALWAYS` is deleted; the
    bare condition remains in production.  The MC/DC denominator
    grows back by the conditions the wrap had folded out, which is
    the correct accounting — the path is reachable.
3.  **Drive the vectors.**  If the now-active condition is
    measurable from script, a coverage test in
    `tests/coverage/coverage_*.tcl` is added so the new MC/DC
    pairs are exercised by the standard suite, not only by the
    saved fuzzer input.
4.  **Iterate.**  The next fuzz campaign now runs against a
    smaller, more honest residue of `NEVER`/`ALWAYS` wraps, and
    any survivor is closer to actually being unreachable.

Each cycle moves a defensive guard from "trusted but unverified"
to either "verified by fuzzer + regression test" or "removed as
incorrect."  The net effect is that coverage and fuzz testing
amplify each other: coverage measures what tests reach, fuzzing
discovers paths the tests missed, and the
`NEVER`/`ALWAYS`-under-`TH8_DEBUG` mechanism is the protocol that
turns those discoveries into corrections to the coverage
denominator itself.  The paper's reported MC/DC figures should be
read with this caveat: they represent coverage of conditions the
authors *currently* believe reachable; ongoing fuzz campaigns
incrementally either validate the remaining `NEVER`/`ALWAYS`
claims or convert them back into measurable conditions.

Two later bug fixes show the mechanism in miniature.  A defensive
slot-index bound added to the secure-variable key-rotation path
guards a condition that cannot occur unless the interpreter's own
state is already corrupt; wrapping it in `NEVER(...)` keeps the
runtime check --- an assertion in debug, a fail-closed branch in
release --- while compiling it to a constant under the MC/DC build,
so it adds no uncoverable decision to the denominator.  Separately,
a missing taint check on one of two command-dispatch paths (Section
6.22) was repaired not by duplicating the compound `TH8_TAINTED(name)
&& Th8_ReportTaint(...)` expression --- which would have added a
second intrinsically-partial two-condition decision, since the
reporter re-tests the same taint bit the guard already tested ---
but by factoring the check into one helper that both paths call.
The helper carries the single compound decision; each call site
becomes a single-condition branch the taint conformance tests fully
cover.  Writing defensive code with the coverage denominator in
mind is not gold-plating; it is what keeps the metric honest as the
safety net grows.


---

## 5.  Agentic AI Integration

### 5.1  The Problem

When an LLM agent generates code for execution, the host
application faces a fundamental trust problem: the generated code
may be correct, partially correct, or actively adversarial (through
prompt injection, training data poisoning, or hallucination).  The
host needs a runtime that:

1.  **Cannot escape its sandbox** --- regardless of what the
    generated code does.
2.  **Has bounded resource consumption** --- a runaway loop or
    exponential allocation must not bring down the host.
3.  **Can be cancelled** --- the host must be able to preempt
    execution at any time.
4.  **Provides provenance** --- the host must be able to
    distinguish scripts from trusted sources (signed) from
    scripts generated on the fly (unsigned).
5.  **Is deterministic enough for testing** --- the same input
    should produce the same output, facilitating verification.

### 5.2  How TH8 Addresses These Requirements

**Sandbox by construction.**  The embedder creates an interpreter
with exactly the capabilities the agent needs.  A typical agentic
deployment would use:

```c
Th8_Platform *p = Th8_ClonePlatform(Th8_GetLibcPlatform());
/* Optionally add read-only data access: */
/* Th8_MergePlatform(p, Th8_GetCurlPlatform()); */
Th8_Initialize(p);
Th8_Interp *interp = Th8_CreateInterp(p);
Th8_RegisterLanguage(interp);
```

This interpreter can evaluate expressions, manipulate strings and
lists, define procedures, and perform arbitrary computation --- but
it cannot read files, open sockets, execute processes, or load
binary extensions.  There is no `exec`, no `open`, no `socket`,
no `file delete`.  These commands simply do not exist.

**Bounded resources.**  The host sets step, memory, and result-size
limits before passing the generated script to `Th8_Eval`.  If the
script exceeds any limit, evaluation terminates with a clean error.

**Cancellation.**  The host can call `interp cancel` from any
thread (or signal handler) to asynchronously terminate execution.
The NRE trampoline checks the cancellation flag between commands
and unwinds cleanly.  The interpreter remains usable after
cancellation.

**Provenance.**  The signed-only policy distinguishes between
trusted (signed) library scripts and untrusted (agent-generated)
scripts.  The host can enable signed-only mode for library loading
but disable it (via save/restore) for agent-generated code,
ensuring that the agent cannot `source` arbitrary files from the
file system even if the file system is accessible.

**Determinism.**  TH8's pure-computation profile (no I/O, no
random bytes, no time) is fully deterministic.  The same script
with the same inputs produces the same outputs, which is ideal
for LLM-generated code verification and regression testing.

### 5.3  Deployment Patterns

**Pattern 1: Computation sandbox.**  The agent generates Tcl
scripts for data transformation (list processing, string
manipulation, expression evaluation).  The host evaluates them in
a Core-profile interpreter with step limits.  No I/O, no risk.

**Pattern 2: Tool orchestration.**  The agent generates scripts
that call host-provided commands (registered via
`Th8_CreateCommand`).  The commands implement tool actions
(database queries, API calls) with the host controlling access
and auditing.  The Tcl script provides the glue logic.

**Pattern 3: Signed library + generated glue.**  The host loads
signed library scripts (utility procedures, validated templates)
under the signed-only policy, then evaluates agent-generated
glue code with the policy suspended.  The generated code can call
library procedures but cannot source additional files.

**Pattern 4: Multi-interpreter isolation.**  Each agent task gets
its own `Th8_Interp` with its own platform, resource limits, and
variable state.  Interpreters share no mutable state.
Per-interpreter library lists and private memory zones (macOS)
provide additional isolation.

### 5.4  TH8 as a Memory Representation Language

A companion document (`agent_memory.md`) develops an argument
that TH8's Tcl-derived syntax is uniquely suited as a memory
representation language for LLMs and autonomous agents.  The core
observation is that Tcl's "everything is a string" principle means
a memory entry is simultaneously human-readable prose, machine-
parseable structured data, and executable code --- a property not
shared by JSON, YAML, Markdown, or any other common memory format.

An agent memory file in TH8 can define factual observations,
preference records, causal chains, and reactive rules using
domain-specific commands (`fact`, `preference`, `caused_by`,
`when`), all of which are valid Tcl.  New memory types are added
by defining new commands; no schema migration is required.
Empirically, the Tcl representation is approximately 3x more
compact than equivalent JSON, a meaningful advantage in LLM context
windows where every token counts.

Memory consolidation --- merging redundant entries, resolving
contradictions, updating stale facts --- can itself be expressed
as a TH8 script rather than requiring external tooling.
The consolidation logic is debuggable, auditable, and version-
controlled alongside the memories it operates on.

### 5.5  Cryptographically Authenticated Memories

TH8's signed-only policy infrastructure extends naturally to agent
memories.  Each memory file can have a companion `.b64sig` Harpy
signature, providing three guarantees: the memory was created by a
trusted party (the signing key holder), the memory has not been
tampered with since signing, and the memory's temporal annotations
(`<<notBefore:...>>`, `<<notAfter:...>>`) are enforceable --- expired
or not-yet-valid memories can be automatically excluded.

This is particularly valuable in multi-agent systems where one
agent's memories are consumed by another.  The consuming agent can
verify provenance cryptographically without needing to re-derive
the memories from scratch.  Combined with TH8's sandboxed evaluation
(memory limits, step limits, no binary loading), even untrusted
memory files can be safely loaded and inspected without risk to the
host.


---

## 6.  LLM-Assisted Development

### 6.1  Methodology

TH8 was developed in sustained collaboration between a human
architect (the author) and a large language model (Anthropic's
Claude, via the Claude Code CLI tool).  This section describes
the methodology honestly, including both its advantages and
its limitations, because we believe this development model is
increasingly relevant and deserves candid discussion rather than
either hype or dismissal.

The collaboration operated as a continuous dialogue over multi-hour
sessions, typically covering architecture design, code generation,
test creation, documentation, and quality assurance in a single
sitting.  The human and the LLM had distinct, complementary roles:

**Human responsibilities:**
-   All architectural decisions (what to build, how to structure it,
    what the security invariants are)
-   Platform-layer discipline (which OS API belongs in which file,
    which abstraction layer a function operates at)
-   Style enforcement (naming conventions, return type semantics,
    parameter ordering)
-   Correctness arbitration ("Tcl 8.4 says X, Eagle says Y, the
    right answer is Z")
-   Final review of every line of generated code
-   Bug diagnosis when the LLM's code failed tests

**LLM responsibilities:**
-   Drafting C implementation code from architectural descriptions
-   Generating test suites (often 30--60 tests per command, with
    edge cases the human would not have thought to test)
-   Writing and updating documentation (the 919-requirement
    standard, man pages, security analyses, this paper)
-   Performing QA passes (finding stale numbers, missing
    cross-references, inconsistent naming)
-   Exploratory refactoring (extracting 11,000 lines from one file
    into 17 plugin files while maintaining all cross-references)

### 6.2  What Worked Well

**Test generation.**  The LLM consistently produced thorough test
suites that caught real bugs.  For the `[dict]` command (20
subcommands), the LLM generated 57 tests including nested key
traversal, empty-dict edge cases, and error paths.  Several of
these tests exposed implementation bugs (e.g., a NULL pointer
dereference in `dict with` with nested keys) that the human had
not anticipated.

**Documentation.**  Maintaining a 919-requirement formal standard,
18 documentation files, and two man pages in parallel with rapid
implementation changes is tedious and error-prone for a human.
The LLM tracked changes across sessions, updated cross-references,
recomputed coverage percentages, and flagged stale content.

**Large-scale refactoring.**  Extracting ~90 commands from a single
11,000-line file into 17 independent plugin files --- while
maintaining all shared state declarations, extern linkage, and
compile-time guards --- is mechanical but demanding work.  The LLM
performed this extraction with high accuracy, and the resulting
code compiled and passed all tests after a small number of human
corrections.

**Exploratory design.**  When the author asked "do you think we
should add an `[after]` command?" or "what would it take to gate
the variable system?", the LLM could rapidly draft a design,
identify cross-cutting concerns, and estimate the blast radius
of a change.  This accelerated decision-making.

### 6.3  What Required Human Correction

**Platform abstraction discipline.**  The LLM repeatedly placed
POSIX-specific functions (e.g., `explicit_bzero`) in the wrong
platform file, or called OS APIs directly from platform-independent
code.  The rule "each platform file must use only APIs from its
abstraction layer" required constant enforcement.  The LLM
understood the rule intellectually but did not internalize it the
way a developer who has been burned by a portability failure does.

**Subtle semantic correctness.**  When TH8's return codes differ
from TH1's (TH_BREAK=2/TH8_BREAK=3 and TH_RETURN=3/TH8_RETURN=2
are *swapped*), the LLM initially used magic numbers instead of
symbolic constants, and sometimes got the mapping backwards.
Type-level correctness (plain `int` for boolean, `TH8_OK`/`TH8_ERROR`
for success/failure, `NULL` for pointer failure) required explicit
feedback.

**Architectural judgment.**  The LLM sometimes proposed
over-engineered solutions (stub functions where `#if` guards
sufficed, excessive abstraction layers, feature flags for
hypothetical requirements).  The human had to consistently redirect
toward the simplest approach.  Conversely, the LLM occasionally
under-estimated the impact of a change (e.g., suggesting that
disabling variables should also disable control flow, when in
fact `[if]`/`[for]`/`[catch]` work fine without variables).

**Build system integration.**  Makefile dependencies, amalgamation
generation, stubs table gating, and MSVC build rules required
significant human intervention.  The LLM could draft Makefile
fragments but lacked the end-to-end understanding of how `make
fresh` sequences through `genstubs`, `mkamal`, compilation, and
linking.

### 6.4  Lessons Learned

1.  **The LLM is a force multiplier, not an autopilot.**  A human
    who knows what they want to build can get there 3--5x faster
    with LLM assistance.  A human who does not know what they want
    will generate 3--5x more code that needs to be thrown away.

2.  **Correctness feedback must be immediate.**  When the LLM
    produced code with a subtle bug, correcting it in the same
    session (with explanation of *why* it was wrong) prevented
    recurrence.  Deferring corrections led to the same mistake
    appearing in multiple files.

3.  **Style rules must be explicit and persistent.**  The LLM
    responded well to concrete rules ("use `argv[0]` in error
    messages so renamed commands show the current name") but needed
    them re-stated across sessions.  A persistent instruction file
    (analogous to TH8's `CLAUDE.md`) was essential.

4.  **The meta-irony is real.**  Building a runtime designed to
    safely execute AI-generated code, using AI-generated code,
    creates a productive tension.  Every security mechanism in TH8
    was tested against the question: "could the LLM that helped
    write this code also exploit it?"  The answer, so far, is no
    --- but the question sharpened the design.

5.  **Deliberate imperfections must be documented at the site, or
    they get "corrected" into regressions.**  A multi-agent audit
    (Section 6.23) flagged an intentional resource leak on the rare
    `[cancel -unwind]` path --- where the callback chain is discarded
    *without invoking the callbacks* precisely so the "unwound" error
    propagates cleanly to the outermost caller --- as a bug.  Acting
    on that finding broke two `suspend` conformance tests before the
    design comment was re-read.  The lesson cuts both ways: the LLM
    re-fixes tradeoffs it does not recognize as deliberate, and an
    audit (human or machine) reading a site in isolation mistakes an
    accepted tradeoff for an oversight.  The cure is a rationale
    comment at the site itself --- not in a design document three
    directories away --- naming the tradeoff and the test that
    enforces it.

6.  **An audit aimed at a goal you will not pursue still pays for
    itself.**  A feasibility study of running TH8 in an operating-
    system kernel concluded the goal was far out of reach --- but the
    same audit, forced to reason about an environment where allocation
    fails routinely and creation must be all-or-nothing, surfaced
    concrete user-mode defects that ordinary review had missed.  The
    sharpest was a constructor that allocated its lookup hashes
    eagerly under a comment reading "so that lookups never need NULL
    checks."  That invariant is real and useful --- but only if
    construction is all-or-nothing, which the unchecked allocations
    quietly were not: an out-of-memory event produced an interpreter
    whose first variable assignment dereferenced a NULL bucket array.
    The optimization ("no NULL checks in the hot path") and the bug
    ("no NULL checks where they were load-bearing") were the same
    missing check seen from two ends.  Two transferable rules fell
    out.  First, a comment that *justifies* an invariant should be
    checked against the invariant's own failure paths: the natural
    cleanup routine for the partial object iterated the very hashes it
    might find NULL, so the teardown tool needed the exact safety it
    was built to make unnecessary elsewhere (its sibling delete
    routine was already NULL-safe; iteration was not, and became so).
    Second, a "can't happen" path earns trust only once something
    drives it: because the runtime's fault-injection layer wraps an
    *existing* interpreter and so cannot fault an interpreter's own
    construction, coverage came from a platform whose allocator was
    overridden *before* creation to fail after N successful requests,
    swept across N, with the heap checker turning any rollback leak or
    double-free into a hard failure.

7.  **An error code that means two opposite things cannot be fixed by
    propagating it.**  The same kernel-feasibility audit found the
    language-registration entry point discarding every sub-result and
    returning success unconditionally, so an out-of-memory event during
    registration produced a "successfully" half-built interpreter.  The
    obvious fix --- stop discarding, propagate the failure --- was
    wrong, because the plugin-registration primitive returned the *same*
    error code for "this plugin is already registered" (a harmless
    re-run during interpreter restore) as for a genuine allocation
    failure.  Propagating it would have turned every legitimate
    re-registration into a fatal error.  The real fix had to *separate*
    the two conditions first --- a small "is it already present?" query
    that lets registration skip an existing plugin but still propagate a
    true failure --- and only then was propagation safe.  The
    transferable rule: when a return value conflates *no-op* with
    *failure*, an over-tolerant caller is a symptom, not the disease;
    the cure is to make the two states distinguishable at the source,
    because neither "keep discarding" nor "start propagating" is correct
    while they share a code.  A corollary bit immediately: surfacing a
    previously-swallowed failure is only half the work --- every caller
    that had relied on the old always-succeeds contract (the shell's
    startup, interpreter restore, child-interpreter evaluation, the
    Fossil embedding, the Tcl bridge) had to be taught to check and
    clean up, or the newly-visible error is simply re-buried one frame
    higher.

8.  **"Graceful degradation" that silently breaks the contract is worse
    than a clean failure.**  The same kernel-feasibility audit found an
    ordered-iteration routine that, when it could not allocate its
    temporary sort buffer, fell back to *unordered* iteration and
    returned no status at all.  On its face this is defensive
    programming --- do something rather than fail --- but it is a trap:
    the routine exists precisely to guarantee insertion order, and under
    memory pressure it quietly abandoned that guarantee while reporting
    nothing, so a caller would emit results in the wrong order and treat
    it as success.  A degraded result that still satisfies the caller's
    contract is graceful; one that abandons the contract's whole reason
    for existing is a silent correctness bug wearing a safety costume.
    The fix was to fail loudly --- return an error and make no ordered
    claim --- rather than to succeed wrongly.  The audit also turned up a
    subtler companion in the same routine: its scratch array was sized
    from a monotonic insertion counter that only ever grew, so it both
    over-allocated after churn and rested on a signed `int` that is
    undefined behaviour to increment past its maximum.  The counter
    feeds an order *comparison*, never an arithmetic value, so the cure
    was neither a wider type nor an expensive renumbering but simple
    saturation: at the ceiling the stamp stops advancing, which leaves
    every earlier ordering intact and merely makes the (unreachable)
    edge defined instead of undefined.  The transferable pair: when a
    fallback cannot honour the contract, surface an error instead of a
    plausible wrong answer; and when a counter's *relative* order is all
    that matters, saturation is a legitimate overflow guard where its
    absolute value would not be.

9.  **A cluster of leaks across many exit paths is a "centralise the
    cleanup" signal, not a call to sprinkle more frees.**  The list-sort
    command inlined its comparison logic --- mode dispatch, sub-element
    extraction, custom-script evaluation --- into the innermost loop of
    an insertion sort, so every one of a dozen early error returns was
    individually responsible for freeing the scratch buffers that
    sub-element extraction had allocated.  Several forgot, leaking on the
    conversion- and evaluation-error paths.  Patching each exit to add
    the missing frees would have been endless whack-a-mole; the durable
    fix was to lift the comparison into a single helper with one cleanup
    point that frees on every return, after which the leaks were
    structurally impossible.  The same refactor incidentally fixed two
    further defects that the inline form had made easy to overlook: a
    dictionary comparison that compared embedded numbers by accumulating
    digits into a fixed-width integer --- undefined behaviour, and simply
    wrong, once a run exceeded about ten digits, which is not an edge
    case --- replaced by comparing digit runs by significant length; and
    an O(n^2) sort that, while cancellable because it polled once per
    comparison, still burned an entire step budget on a million-element
    list, a denial-of-service lever that "it can be cancelled" does not
    excuse.  A stable O(n log n) merge sort was a drop-in once the
    comparison was a callable function.  The lessons compound: **inlining
    a concern that has its own resources and failure modes into a hot
    loop multiplies the places that must each get cleanup, numeric
    correctness, and complexity right; extracting it makes each provable
    once.**  A closing note on regression tests: verifying the new
    error-reporting against a reference Tcl caught a test that pinned a
    *deliberate* divergence (the runtime treats an out-of-range index
    leniently where Tcl errors), a reminder that a regression test should
    freeze intended behaviour, not accidentally ratify a known,
    intentional departure from the reference.

10. **A comment that names a safety mechanism which does not exist is
    worse than no comment.**  The expression engine's source --- and the
    security model document --- stated that expression recursion was
    "bounded by the evaluation-depth counter."  It was not: nothing in
    the expression parse, evaluate, or free path ever touched that
    counter.  The only thing between a hostile `((((...))))` or
    `1+1+...+1` and a native-stack overflow was a host-specific stack
    probe that cannot be relied upon for a portable depth proof.  A
    missing comment would have invited the question "what actually bounds
    this?"; the confidently wrong comment closed it, and an audit reading
    the site in isolation believed the bound existed.  When a comment
    says "safe because X," X has to be true and checkable at that site.
    Fixing it surfaced a second, subtler hazard: the tree *teardown* was
    recursive and completely unguarded.  The evaluator at least polled at
    each node, so a too-deep expression errored cleanly --- but the free
    ran afterward on the tree that had already been built, and an
    operator chain builds a tree whose depth equals its length, so the
    safest input for the evaluator (rejected immediately) still handed
    the recursive free a structure deep enough to blow the stack tearing
    it down.  Cleanup paths inherit the depth of the structures they
    walk; a recursive destructor is exactly as dangerous as a recursive
    evaluator and easier to overlook because it does no "work."  The fix
    made the free iterative (freeing a binary tree in constant stack via
    left-rotation) and replaced the imaginary bound with real ones placed
    where the recursion actually lives --- a builder that bounds
    parenthesis nesting, a per-interpreter counter that bounds the
    evaluator at every call site, both snapshotted across coroutine
    switches.  The general lesson: bound the recursion that exists, at
    the site it exists, and never let documentation stand in for a
    mechanism.

11. **A test whose assertion is true for both the pass and the fail case
    is not a test.**  The sandbox resource suite was meant to prove that
    abusive inputs (regular-expression bombs, string amplification, list
    bombs) are stopped by a resource limit.  Several tests asserted only
    that the sandbox returned *some* status code, or merely that it
    returned a non-zero one --- predicates satisfied whether the limit
    fired, the input happened to finish, or an unrelated bug swallowed
    the error.  Such a test confirms the process did not crash and
    nothing more.  The remedy was to instrument first: a throwaway probe
    printed the real outcome of each scenario --- return code, message,
    step count, bytes allocated --- before a single assertion was
    written, so each test could then pin the *specific* mechanism that
    terminated the attack and the boundary it fired at.  Instrumenting
    first paid for itself immediately by exposing what the vague
    assertions had hidden: one test's stated premise was simply false
    (the regex bomb is bounded by a dedicated regex-complexity limit, not
    the step counter it claimed, and regex backtracking does not
    increment that counter at all), and another was a silent no-op that
    looped over an operation which, for its inputs, did nothing and so
    exercised none of the behavior it named.  A test that green-lights a
    mechanism it never runs is worse than no test, because it reads as
    coverage.  The transferable discipline: dump the real observable
    state before asserting on it, and make every assertion
    discriminating enough that the fail case would actually fail it.

12. **The same bug class recurs in every subsystem with an
    attacker-controlled recursion --- and a bound on one construction
    path does not bound an equivalent path.**  Lesson 6 fixed an
    unguarded recursive teardown of the expression tree.  Weeks later,
    evaluating a finding the kernel audit had filed as "kernel-only"
    (make teardown iterative for tiny kernel stacks) for its user-mode
    value, we found the *identical* defect in a different subsystem: the
    namespace tree.  Deleting a deeply nested namespace recurses once per
    level in the destructor and overflows the stack --- crashing even the
    8 MiB main-thread stack once nesting reached tens of thousands, and a
    worker-thread stack far sooner.  What made it evade the earlier sweep
    is the sharper half of the lesson.  The obvious way to nest
    namespaces --- writing `namespace eval a { namespace eval b { ... } }`
    --- *was* bounded, because each body is a nested evaluation and the
    evaluation-depth limit caught it.  But the *same* structure could be
    built by a single command with a deeply qualified name
    (`namespace eval a::a::...::z {}`), which creates one level per name
    component in one evaluation and so slips past the depth limit
    entirely.  A resource bound placed on the intuitive construction path
    is not a bound on the resource; it is a bound on that path.  When a
    structure can be built more than one way, the limit has to live where
    the structure is *used* (here, a depth field on the namespace itself,
    checked at every creation, mirroring the expression-depth cap), not
    on any single builder.  Two compounding morals: audit for a fixed bug
    *class*, not the fixed bug --- every place the codebase recurses over
    attacker-shaped data is a suspect until checked; and never let a
    finding's stated environment ("kernel-only") decide its relevance
    before asking whether the underlying defect is environment-specific.
    This one was a real, reachable, main-stack crash in the shipping
    user-mode interpreter, hiding inside a "kernel" finding.

13. **A feature flag that every build enables tests only half the
    code; the configuration nobody compiles has zero coverage, and it
    is usually the one shipped.**  TH8's signed-script tests need a key
    to sign with, so every debug, MC/DC, and test recipe passes
    `ENABLE_TEST_KEY=1`.  Convenient --- and it meant the entire matrix
    compiled one side of every `#if TH8_ENABLE_TEST_KEY`.  A
    crypto-*with*-test-key build and a crypto-*without*-test-key build
    are different programs, and only the former was ever built.  So
    when the authenticated-time verifier began calling the
    test-key-only accessor `Th8_GetEmbeddedKeyTest` unconditionally
    --- against a symbol declared and defined solely under that flag
    --- nothing in our own CI noticed: the flag that would have exposed
    the break was set in every job.  It surfaced only when the
    amalgamation was regenerated for a downstream embedding (Ladybird)
    that runs the genuine production configuration --- cryptography on
    for signature verification, no test key --- i.e. at the vendor, not
    the source.  The sharp edge is that the *test-only convenience*
    flag (a test key, a fault-injection shim, a debug hook) is exactly
    the one whose *absence* deserves its own build, because production
    ships with it off: the more universally a flag is set "to make the
    tests work," the more completely its off-arm escapes testing.  The
    durable fix was not the one-line guard that stopped the bleeding
    but treating the build matrix as part of the test suite: we added a
    dedicated `check-amal-notestkey` gate that regenerates the
    amalgamation and compiles it with cryptography on and the test key
    explicitly off --- the precise configuration no other job builds
    --- so the next unconditional reference to a test-key-gated symbol
    fails at the source instead of in a consumer's tree.  A
    reverted-fix run confirmed the gate reproduces the original
    compile error.  The general moral: a configuration that no job
    compiles has zero coverage no matter how green the suite looks, and
    "it builds for us" is never evidence it builds for the consumer who
    flips the flag the other way.

14. **A fault injector that fails-and-stays-failed cannot find the bugs
    that need the allocator to recover.**  To exercise out-of-memory
    handling we had a construction-time injector that returned NULL for
    a chosen allocation *and every allocation after it*.  It swept the
    trip point across the constructor and caught crashes, double-frees,
    and leaks under a debug allocator --- and it passed.  But a
    persistent failure hides an entire bug class: once allocation N
    fails, allocation N+1 fails too, so any code that checks N+1 rolls
    back and exits before the damage from an unchecked N can manifest.
    A skeptical re-audit demanded a *one-shot* injector --- fail exactly
    one allocation, then resume normal service --- to model a genuine
    transient OOM.  With that change the very first sweep crashed: the
    constructor ignored the return value of the routine that pushes the
    global call frame, which allocates the frame's variable table and,
    on failure, returns an error *without* installing the frame.  Under
    the persistent injector the next allocation (the global namespace)
    also failed and its own check rolled construction back cleanly, so
    the unchecked frame push was never reached; under the one-shot
    injector the allocator recovered, construction marched on with a
    NULL frame, and global-variable setup dereferenced it.  The bug was
    real, reachable, and had been sitting behind a green out-of-memory
    test suite.  Two morals: an OOM test is only as strong as the
    *shape* of the failure it injects --- persistent and one-shot
    (and, ideally, fail-the-Nth-only) find different defects --- and a
    coverage drive that merely runs the rollback path without asserting
    the resulting status or state will report success while a partial,
    corrupt object escapes.  We now assert, for every injected trip,
    that a surviving object is *complete*, not merely non-crashing.

15. **Persistent and one-shot fault injection are complementary --- and the
    assertion has to match the tool.**  Lesson 14 showed a one-shot
    injector finding a crash a persistent one masks.  Hardening a second
    entry point --- the routine that registers the built-in language ---
    showed the converse and completed the picture.  A one-shot fault
    (fail exactly one allocation, then resume) is a *crash finder*: with
    the allocator recovering, execution runs past an unchecked failure
    into the dereference of the resulting null pointer, which is exactly
    how it surfaced a family of unchecked hash-insert dereferences in the
    package commands.  But the natural one-shot invariant "if a fault
    fired, the operation must report failure" is simply *false* for any
    subsystem with legitimately recoverable allocations: a single
    transient failure that the code tolerates --- a fallback, an optional
    or retried allocation --- completes with a correct, complete result.
    Registration had roughly five hundred such trips, none of them bugs;
    asserting the one-shot sweep with a failure-required invariant
    produced five hundred false alarms.  The all-or-nothing *assertion*
    instead wants a *persistent* fault (fail this allocation and every
    later one): a persistent failure cannot be recovered, so "fired
    implies error" becomes a clean invariant, and any success is a real
    partial-success defect.  That persistent sweep flagged two genuine
    ones --- an unchecked string duplication, and a system-variable
    declarator that returned success after failing to mark the variable,
    leaving the security-policy array writable from scripts behind a
    registration that reported complete.  The discipline that falls out:
    injectors have a *shape*, assertions have a *shape*, and they must be
    paired --- one-shot with "no survivor is partial and nothing
    crashes," persistent with "every fire is an error."  A subtler trap
    on the same theme: a fault injector's "it fired" signal must mean an
    allocation was actually failed, not merely that its trip counter
    reached zero, or the very last allocation of a run reads as a
    spurious violation.

16. **A thread-safety claim on the box is a synchronization obligation ---
    and only a concurrency test with a sanitizer proves you met it.**  One
    API was documented callable from any thread, and it did set its
    "canceled" bit atomically.  But the accompanying error MESSAGE was
    published through two separate `volatile` fields, a pointer and a
    length.  `volatile` orders a single location on real hardware; it does
    not make a two-field update atomic.  Two threads cancelling at once
    could interleave so the reader adopted one thread's pointer with the
    other's length --- a heap over-read hiding behind an API that
    advertised thread safety.  The lesson is first that an atomic flag is
    not thread safety when the payload is multi-field: either narrow the
    contract to a single producer, or publish the whole request as one
    unit under a lock; a bag of `volatile` fields does neither.  The
    second, sharper half is about the *test*.  The existing "concurrency"
    test started one canceller and joined it before inspecting the result
    --- its own comment observed that the join made the outcome
    deterministic, which is to say it exercised no race whatsoever.  We
    replaced it with a stress that launches several cancellers
    simultaneously behind an atomic start gate while the owner races them,
    and checks message coherence on every observation.  Run under
    ThreadSanitizer it immediately reported one race we had not reasoned
    through by hand: a lock-free fast-path read of the "request pending"
    flag, the classic double-checked-locking hazard where an atomic read
    races a non-atomic write.  Making that single flag fully atomic
    cleared it, and the sanitizer then certified the whole path.  A
    concurrency claim you cannot demonstrate under a race detector, with
    genuinely simultaneous actors, is a claim you have not tested ---
    however carefully you reasoned about the code.

17. **A poll before a loop is not a poll in the loop; an opaque library
    sort is a cancellation black hole; and "it already polls" is a claim
    about every loop, not every function.**  A scripting engine that runs
    untrusted input must stay interruptible, so its long loops check a
    readiness flag periodically.  We believed that work was done, and a
    skeptical re-audit found three ways the belief was wrong --- each the
    same illusion, that a function which *mentions* the readiness check is
    interruptible everywhere it iterates.  The merge sort polled before
    every comparison, which looks thorough, but when one run is already
    ordered it drains the other and copies back with no comparisons at all,
    and those copy loops had no poll --- roughly a million uninterruptible
    iterations at the ceiling.  The ordered hash iterator polled once and
    then handed the entire attacker-sized array to the C library's sort,
    whose comparator has no way to reach the interpreter and so cannot
    poll: the sort itself was uncancellable, and no amount of polling
    *around* the call could fix it --- the remedy was to own the sort, a
    pollable merge sort (the same stable algorithm the language's own list
    sort and native Tcl use) that checks readiness inside its own loops.
    And the string-split command did not merely fail to poll; on
    cancellation it jumped to a shared exit that replaced the pending error
    with the partial result it had built so far and returned success ---
    reporting truncated output as if it were complete.  The checker that
    was supposed to guard all this verified only that each listed function
    contained the text of the readiness call *somewhere* in its body, which
    is not the same as the poll being inside each loop, on a bounded
    interval, with its failure propagated rather than swallowed.  Three
    rules fall out, and they are the ones a reviewer should carry into any
    interruptible interpreter: put the check in every loop that an
    adversary can make long, not just the one that dominates the typical
    case; never delegate attacker-sized work to a comparator or callback
    the cancellation signal cannot reach --- own the loop; and treat a
    readiness failure as an error to propagate, never as a shorter answer
    to return.

    Closing the finding taught two more lessons that the first three did
    not.  The first is that *enforcing a hand-written list and discovering
    what the list forgot are different guarantees, and a serious audit ships
    both.*  We strengthened the checker so it no longer accepts the mere
    presence of the readiness call: it now parses each listed function and
    fails unless the poll sits inside a `for`/`while` scope on a bounded
    interval --- exactly the placement error it had been blind to.  But a
    checker over a list can only prove that the loops you already named still
    poll; it is silent about the loop you never added.  So we wrote a second
    tool that sweeps the entire attacker-reachable command surface and reports
    every function containing a loop that is neither polled nor recorded, with
    a documented reason, as bounded --- and made a zero-unaccounted result a
    build gate.  That sweep immediately found loops the careful named audit
    had missed: several string operations iterated over the *value* of their
    argument, byte by byte, with no poll.  Which surfaced the distinction that
    makes the whole inventory tractable: a loop over a command's argument
    *vector* is bounded by the parse that produced those arguments and needs
    no poll, but a loop over an argument's *value* --- an attacker-sized
    string or list --- is unbounded and must.  Conflating the two is what let
    the gaps hide in plain sight.  The second lesson is about the test: "it
    eventually stops" is not a latency bound.  A binary did-it-cancel assertion
    cannot tell a one-millisecond overshoot from a ten-second one, and it is
    the *number* that the poll interval is supposed to guarantee.  We added a
    measurement that runs a compute-bound infinite loop under a real
    wall-clock deadline and returns the actual elapsed time, then asserts it
    against a bound; a hundred-millisecond deadline that overshoots by about a
    millisecond is evidence, where "it returned an error" is only an anecdote.

    The most instructive part of the closure was that these two new tests
    caught a poll *we ourselves had just added in the wrong place*.  In the
    zeal to leave no loop uninterruptible, we had put a readiness check inside
    the parser --- the routine that scans a single word, and the one behind the
    "is this script complete" query.  Both regressed immediately.  The measured
    step-ceiling test failed because the parser runs on every word of every
    command, so a check that increments the step counter once per word inflated
    a legitimate fifty-thousand-iteration workload past a limit it used to fit
    under with room to spare.  And the suspend/resume test failed because the
    parser is also on the debugger's path: when a breakpoint freezes the
    interpreter, the readiness call returns a distinguished "suspended" status,
    and the parser --- which has no way to save and resume its position
    mid-word --- treated that status as a fatal error and abandoned the rest of
    the script on resume.  The fix was to *remove* the polls and record the
    parser as bounded: scanning input is a linear pass whose length is bounded
    by the size of the input the attacker already had to supply (or, for the
    completeness query, by the size of a value that already occupies
    memory) --- there is no amplification to interrupt, and cancellation of the
    surrounding work belongs to the evaluation loop, which polls once per
    command.  The lesson generalizes past this codebase: a readiness poll is not
    free instrumentation you can scatter everywhere, because it is a *semantic*
    act --- it consumes a unit of the very budget it guards, and it can observe
    states (suspended, step-exhausted) that the enclosing code may not be
    structured to handle.  A loop that is already bounded, or that sits on a
    path where the cancellation signal has a meaning the code cannot honor,
    should be *dispositioned* bounded with a written reason, not reflexively
    polled.  Distinguishing the two requires exactly the pairing this finding
    forced into existence: an inventory that makes you justify every loop, and
    probative tests --- one that measures the resource ceiling, one that
    exercises suspend and resume --- sharp enough to tell a helpful poll from a
    harmful one.

18. **Harden the WRITE to a resource counter, not just the comparison that
    guards it; and when a counter can only err one way, pick the safe way.**  A
    sandboxed interpreter caps the memory one script may consume by comparing a
    running byte counter against a limit before each allocation.  We had already
    made that *comparison* overflow-safe --- rewriting `used + request > limit`
    as a subtraction so the sum could not wrap past the check.  The defect was
    one line later, in the counter's own *update*: after a successful
    allocation the counter was incremented by the allocator's reported *usable*
    size, which on real allocators exceeds the requested size by rounding, with
    a plain `+=`.  That add could wrap the machine word, and once it did the
    counter became small, every later comparison passed, and the cap silently
    ceased to exist --- for an ordinary, non-malicious allocator.  A correct
    guard in front of an unchecked write buys nothing; the write is as much a
    part of the invariant as the check.  The fix routes every update through one
    helper that adds with an overflow test and, on overflow, *saturates* the
    counter at its maximum --- which makes every future check fail, poisoning the
    interpreter locally, rather than wrapping or aborting the process.  The
    subtler half is a design choice the fix forced us to reason about out loud.
    The finding asked us to keep the counter at or below the limit at all times,
    and the tidy way to do that is to clamp the running total down to the limit
    whenever an allocation's rounding would push it over.  That clamp is
    *unsound*: the matching free subtracts the block's real usable size, and
    subtracting a real size from a clamped total drives the counter *below* the
    true outstanding bytes --- understating memory pressure, the dangerous
    direction, and over repeated allocate/free cycles at the boundary it lets
    real usage drift past the cap.  We instead keep the accounting *exact* and
    saturate only against wrap, accepting that honest rounding can leave the
    total a single allocation's rounding *above* the limit.  That is the safe
    direction: the next check is merely stricter and rejects a moment sooner,
    and the overshoot is erased the instant the block is freed.  The general
    rule for any monotonic resource counter that can only be slightly wrong:
    prefer the formulation that over-counts (rejects sooner) to the one that
    under-counts (admits more), even when the under-counting one satisfies the
    literal invariant more neatly.  A bound that always errs safe beats an
    equality that sometimes errs dangerous.

19. **"Unreachable in practice" is a claim about the counter's lifetime, not the
    collection's size --- and an unreachable guard is still worth *driving*, not
    waiving.**  Our ordered dictionaries preserve insertion order by stamping
    each entry with a monotonically increasing sequence number and sorting on
    it.  The stamp was a 32-bit integer, and a comment reassured the reader that
    it could not overflow "under the element ceilings in practice" --- a
    dictionary is limited to some millions of entries, and millions fit in 32
    bits with room to spare.  The reassurance confused two different quantities.
    The stamp counter is not bounded by how many entries are *live*; it advances
    on every insert and is never rolled back on a delete, so it is bounded by the
    *total inserts over the structure's lifetime*.  A dictionary used as a work
    queue --- push an item, pop an item, repeat --- holds a handful of entries at
    any instant yet drives the counter through two billion in a session that runs
    long enough, and at that point every later entry gets the same stamp and the
    order the API promises silently degrades to arbitrary.  Reasoning about a
    lifetime counter from the size of the collection is the trap; the quantity
    that matters is the integral of increments over time, which for an
    insert-stamped structure is unbounded by the live size.  The fix is
    unglamorous --- widen the counter to 64 bits, where the ceiling recedes to
    centuries of continuous churn --- but the testing choice is the more general
    lesson.  A 64-bit ceiling is now genuinely unreachable by any workload, so
    the branch that handles saturation looks like dead code a coverage tool can
    only be told to ignore.  Instead of waiving it, we drove it: a test primes a
    throwaway table's counter to one below the ceiling through the public API and
    inserts across the boundary, asserting that the last distinct entry still
    sorts first, that entries at the ceiling tie, and that the counter does not
    roll past its maximum.  That single test both colors the "impossible" branch
    covered and freezes the boundary's semantics as a regression --- two things a
    waiver cannot do.  Whenever an unreachable branch guards a field you can set,
    priming the field beats excusing the branch.

20. **A resource test must assert the quantity that distinguishes correct from
    plausibly-wrong: the peak, not the residual; the exact boundary, not an
    inequality; the terminated instance, not a fresh one.**  A sandbox that
    bounds an untrusted script's memory, steps, and time is only as trustworthy
    as the tests that pin those bounds, and a recurring weakness is tests that
    assert a direction where the interesting fact is a number or a state.  Three
    examples from tightening ours.  First, the sandbox reported the *current*
    bytes allocated, so a test could not tell a script that briefly built a
    large structure and freed it from one that never grew --- the residual after
    cleanup is near zero for both, which is precisely the quantity a memory
    bound is not about.  The fix was a high-water gauge that records the maximum
    ever held; notably it cost almost nothing because an earlier change had
    already routed every allocation through a single accounting helper, so the
    peak update was one comparison in one place --- consolidation paying a
    dividend a second time.  Second, the step-limit tests asserted that a bomb
    burned *more than* the million-step limit, a condition every runaway also
    meets; because the counter is checked once per step, termination lands
    deterministically one step past the ceiling, so the test now asserts exactly
    that count and thereby proves the limit is a precise ceiling rather than a
    loose upper bound.  Third, the "does the system survive a limit breach" test
    ran the next script in a *fresh* interpreter, which shows the host is
    unharmed but says nothing about the interpreter that was terminated; a
    same-instance test reuses that interpreter and shows it stays bounded until
    its counter is reset and then works again --- distinguishing "contained" from
    "permanently broken," a distinction a fresh instance cannot witness.  The
    through-line, and the reason these belong beside the coverage lessons: a
    probative test chooses the *observable* that separates right from
    almost-right --- peak versus current, equality versus inequality,
    same-instance versus fresh --- which is the measurement analogue of driving a
    decision's exact arm instead of merely reaching the code.  An assertion that
    a wrong implementation would also satisfy is not evidence, however green it
    runs.

21. **When a system already expresses a capability one way everywhere, the
    cheapest place to extend it is the lone spot that does it differently ---
    conforming the outlier often deletes code instead of adding it.**  We needed
    per-interpreter control over which ensemble sub-commands (`string toupper`,
    `file delete`, and ~120 others) an untrusted interpreter may call.  The
    reflexive design is a gate: a per-interpreter allowlist consulted on every
    dispatch.  We built that prototype --- a per-ensemble bitmask checked in the
    shared dispatcher --- and discarded it, because it answered the wrong
    question.  Every other slice of the command surface --- top-level commands,
    math functions, system variables --- was already a *per-interpreter registry*,
    and "may this interpreter use X?" was already answered by "is X registered in
    this interpreter?"  Sub-commands were the single exception: a process-global
    `const` table dispatched through one file-static chokepoint.  So the feature
    did not need a new mechanism; it needed the exception to stop being one.
    Promoting sub-commands to a per-interpreter hash on the command record ---
    availability modeled as mere presence, the way the rest of the system already
    worked --- let the dispatcher, the introspection path (`info subcommands`),
    and the "unknown subcommand: must be a, b, or c" error all read one structure,
    so they cannot disagree by construction, and it required no permission word on
    the hot path.  Two dividends fell out.  First, the eleven trivial per-ensemble
    delegator functions and the shared dispatcher they fed all became dead code
    and were deleted: the feature's foundation shipped as a *smaller* codebase.
    Second, unifying the outlier exposed a latent bug it had been hiding --- four
    of the eleven ensembles kept their sub-command tables function-local, so the
    introspection command had a silent blind spot for exactly those four, which
    the conformance work fixed as a side effect rather than a separate task.  The
    general lesson for both human and LLM designers: before adding a gate, ask
    whether the thing you are gating is the one component that is not already
    shaped like its siblings; making it conform is frequently a net-negative diff
    that removes an entire class of "two views of one truth" bugs at the same time.

22. **Every public, supported knob must be safe across arbitrary combinations of
    its values --- and the all-features-off build is the single test that proves
    it.**  If a build-time flag is documented and supported, then every combination
    of its values with every other supported flag is part of the contract, whether
    or not any build exercises it.  The architecture claimed --- in this paper and
    in the portability guide --- that any optional feature could be compiled out,
    down to a minimal build with everything disabled.  A cross-configuration sweep
    that toggled each feature and each plugin off, one at a time, found five
    configurations that did not so much as link.  None were regressions; they were
    standing gaps that the default build --- which enables everything --- had simply
    never exercised, the same blind spot Lesson 13 describes for the test-key flag
    but generalized to the whole feature matrix: a supported knob whose off-position
    nobody compiles has zero coverage, a modularity guarantee is worth exactly the
    build that enforces it, and documentation cannot stand in for a compile.  The
    breaks came in two shapes worth separating.  The common one was a *coarse* guard:
    a use-site that depended on two features but was conditioned on one.  Secure
    variables need both cryptography and the variable system, yet their sites read
    `#if CRYPTOGRAPHY` alone, so the cryptography-on / variables-off corner broke; a
    test driver sat inside a cryptography block but called the fault-injection API
    without guarding it, so it broke only in the narrow cryptography-on / fault-off
    corner.  A one-feature guard cannot express a two-feature dependency, and a
    feature block that wraps "everything crypto-ish" hides which of its lines
    actually need which capability.  The fix is to push the condition down to the
    site and make it a conjunction where the site needs two --- `#if CRYPTOGRAPHY &&
    VARIABLES` --- which is also the only form under which each dependency is
    independently removable.  The rarer shape was the mirror image: general test
    infrastructure parked inside a feature's block for proximity, not dependency, so
    disabling that feature took out unrelated machinery; one un-gating cleared a
    dozen phantom breaks.  A latent offender underneath both was a hand-maintained
    pair of stub tables that list which symbols are feature-gated --- exactly the
    drift-prone parallel list Lesson 21's sibling finding warns against --- where one
    gated public API missing from the list referenced its symbol unconditionally and
    went undefined the instant the feature was off; the durable answer was not to fix
    the one entry but to add a gate that re-derives each symbol's guard from the
    header's own `#if` context and fails if the list omits it.  The capstone is the
    build with *every* feature off at once: each single-feature-off build proves only
    that one flag's dependencies are gated, but the all-off build forces every
    per-site guard to hold simultaneously and so surfaces any remaining ungated
    dependency in a single pass --- it is the closest a discrete matrix comes to
    proving safety over *all* combinations at once.  We moved that build out of the
    prose and into the build matrix as a first-class phase that runs on every sweep.
    The transferable rule for any project that exposes configuration: the set of
    supported values is a promise that every combination of them works, so the
    minimal configuration is a test rather than a footnote --- put it in the matrix,
    gate each dependency at the site that has it, and use a conjunction the moment a
    site needs two features rather than one.

23. **A scope that resets a global counter for a "fresh start" owns restoring the
    truth at scope exit, not the snapshot it took --- and you cannot test one
    component's fault handling through collaborators that mishandle the same
    fault.**  The `[try]`/`[finally]` command grants its cleanup block a fresh
    memory budget so that finally can run even when the try body has exhausted the
    allocation limit: it saves the byte counter, zeroes it, runs the block, and
    restores the saved value.  That restore was the bug.  Any memory the finally
    block left *live* --- a global it set, a channel it opened --- vanished from the
    accounting the instant the saved value was written back, so a script could hold
    memory far past its cap while the counter read near zero (a direct probe
    retained tens of megabytes under a sixteen-megabyte ceiling reporting under a
    hundred bytes), and the next allocation sailed through.  The whole apparatus of
    a single-owner memory limit was defeated by one plugin restoring a stale
    snapshot.  The fix is exact rather than approximate: because the counter was
    zeroed for the block, its value *after* the block is precisely the block's live
    residue, so restoring `saved + residue` (saturating) both preserves the
    fresh-budget behavior during the block and charges what the block kept.  The
    general rule: **a snapshot taken before a scope is only a correct thing to
    restore if the scope leaks nothing --- which is exactly what a cleanup block
    exists to not promise; a scope that resets shared state must reconstruct the
    truth on exit, not reinstate the past.**  The same recheck found the neighbours
    this class always travels with --- a save that *moved* an owned buffer out of
    the interpreter but forgot to free it on the path that discards it, and a result
    copy-and-restore that ignored allocation failure in three places and so reported
    success with a silently emptied result --- each the same shape as Lesson 6's and
    Lesson 8's unchecked hand-offs, and each fixed by routing every exit through one
    checked helper.  But the sharpest lesson came from *testing* the result-loss
    fix.  The natural probe --- sweep a one-shot allocation failure across a
    `try { produce a known result } finally { ... }` and assert the command never
    returns success with the wrong result --- reported failures that were not the
    try machinery's fault at all: the body commands themselves (`format`, `string
    repeat`) degrade silently under memory pressure, returning success with an empty
    string, which `[try]` then faithfully propagated.  The harness was measuring its
    own instruments.  Isolating the component under test required abandoning the
    command sweep for a deterministic allocation limit sized so the body's result
    materializes exactly once but the machinery's *copy* of it cannot --- making the
    copy the only thing that can fail.  The transferable half: **you cannot validate
    that component X handles a fault correctly by observing it through collaborators
    that handle the same fault incorrectly; the test must drive the fault into X
    alone, or X's correct behavior and a collaborator's bug are indistinguishable in
    the output.**  (That the built-in commands degrade silently under OOM is a real,
    separate defect the exercise surfaced and the project logged for its own sweep.)

24. **A return value that every caller ignores is a return value that does not
    exist --- so nearly the entire command surface reported success with an empty
    result under memory pressure, and only a differential oracle could see it.**
    The sweep Lesson 23 promised found that the silent OOM degradation was not two
    commands but essentially all of them.  The mechanism was uniform and dull:
    roughly three hundred call sites end a command with `SetResult(...); return
    OK;`, and the result setter *copies* its argument --- a fresh allocation, which
    is why the caller frees its buffer right after --- so on out-of-memory it clears
    the result and returns an error that no one reads.  The compiler cannot warn:
    the return type is honored, the value is simply dropped.  A command that
    "cannot fail" fails silently the instant the allocator does, and a sandboxed
    script that hit its memory limit would march on with an empty string where it
    expected data.  Two things make this lesson worth more than "check your return
    values."  The first is *why the existing tests could not see it.*  The runtime
    had a substantial out-of-memory test suite --- a fault-injection layer,
    per-call-site filters, dozens of sweeps --- and it certified the wrong property.
    Every one of those tests asked "did we reach this branch under fault without
    crashing or leaking," never "is the answer the command returned correct or an
    honest error"; two of them even carried comments declaring the silent
    truncation acceptable *because the goal was reaching the branch.*  A
    fault-injection suite that only proves reachability is measuring coverage and
    calling it correctness, and no number of such tests would ever have flagged an
    empty-but-successful result.  The test that finds this class is *differential*
    and it must be written as an oracle rather than as per-command assertions,
    because three hundred hand-written checks is not a thing anyone maintains: run
    each command once cleanly to capture a baseline, then sweep a single allocation
    failure across every allocation it makes, and flag any trip where the fault
    fired, the command claimed success, yet the result differs from the baseline
    (list-aware, so a `{a} b` requoting of `a b` is recognized as the same list, not
    a regression).  That harness turned an untestable folk belief ("we handle OOM")
    into a build gate.  The second is *the shape of the fix.*  The instinct is three
    hundred edits; the right move is one chokepoint, and it was already built.  The
    dispatcher every command is invoked through sets a per-interpreter "a result
    build failed" flag from the handful of result-producing primitives, clears it
    before each command, and promotes a would-be success to an out-of-memory error
    when the flag is set --- saved and restored around the call so nested commands
    neither mask nor are masked.  Five edits, and the entire surface conforms; the
    full suite, exercising the hottest evaluation paths, passed unchanged, which is
    the evidence that the scoping is right.  This is Lesson 21's "conform the
    outlier" inverted: there, one component differed from a uniform surface and the
    cure was to make it conform; here, a uniform *mistake* spanned the surface and
    the cure was to enforce the missing invariant at the one point they all pass
    through.  The closing note is that an ignored allocation-failure return is a
    silent wrong answer in the best case and a memory-safety bug in the worst: the
    same sweep found a command that ignored its list-append failures, left its
    accumulator truncated to an odd length, and then read that list as key/value
    pairs --- an out-of-bounds index and a hard crash --- which is the identical
    defect (a dropped failure return) that had merely been returning wrong strings
    everywhere else.  Only a test that drives the fault and inspects the *observable
    outcome*, not the branch reached, tells any of these apart.

25. **A guard with one structurally-dead arm is permanently capped at 50% MC/DC,
    and "50%" looks the same whether the covered pair is the meaningful one or the
    dead one --- so the waiver decision must be read from the per-decision vectors,
    not guessed, and the live arm must still be driven for the waiver to be honest.**
    Lesson 19 is about the branch you *can* reach if you set the field: drive it,
    because a waiver cannot pin the boundary.  Its mirror is the branch you
    *cannot* reach because the two conditions of a compound guard are correlated ---
    a validated-key modulus pointer that is never non-NULL with a zero length, a
    timestamp field pre-validated as fixed-width digits so its value can never fall
    below zero or need a fifth digit, a "not before" that a prior check already
    rejected against the same clock.  For these the honest move is the opposite of
    Lesson 19 --- waive --- but only after establishing two facts from evidence.
    First, *which* independence pair is unformable: the tool's per-decision output
    ("C1-Pair covered, C2-Pair not covered") plus the source invariant that pins C2
    at a constant, because a rebuild spent driving the *dead* arm proves nothing and
    a blanket "decision unreachable" waiver over a guard whose live arm is a real
    boundary is simply a lie.  Second, that the live arm *is* exercised: the four
    timestamp guards each cap at 50%, but the covered half is a genuine
    out-of-range certificate (year 1969, hour 25) that a fixture drives on purpose,
    so the waiver excuses only the truly-dead half.  Two mechanical traps flank the
    judgment.  The waiver's classification tag is a closed vocabulary the gate
    validates --- an invented tag fails with "unknown class," not a soft warning ---
    and its file column must be the path the coverage map records (from the source
    root, not a bare basename), or the waiver silently matches nothing and leaves
    the decision in debt while itself reading as stale: a double failure one error
    can mask the other of.  The gate re-reads waivers without a rebuild, so the
    discipline is cheap: edit, re-run the gate, and refuse to proceed until the
    stale-waiver count is zero.

### 6.5  Case Study: The Spilornis Buffer Overflow

The integration of Spilornis --- Eagle's list parser, used for
Tcl-compatible list splitting --- illustrates both the value and
the limitations of LLM-assisted code review for security-sensitive
code.

The Spilornis library was initially integrated with several issues.
An LLM session identified important fixes and recommended changes
to the UTF-8 backslash escape processing in `EagleParseBackslash`,
which were implemented and tested.  A subsequent session flagged a
potential buffer safety concern: the backslash parsing algorithm's
`USE_NARROW_CHAR_T` code path could produce up to 4 output bytes
(a full UTF-8 encoding) from a single 2-character backslash
sequence, but the buffer in `Eagle_SplitList` was allocated
assuming a 1:1 input-to-output byte ratio.

A further round of LLM analysis concluded the concern was not a
real issue.

AFL++ fuzzing of the `fuzz_list` harness later discovered 24
concrete crash inputs proving the concern *was* real.  All 24
triggered heap-buffer-overflows in the backslash expansion path.
The unsafe multi-byte UTF-8 expansion was removed from both TH8's
copy of Spilornis and the upstream library.  TH8's own UTF-8
handling (`Th8_Utf8Encode`/`Th8_Utf8Decode`) was unaffected ---
it is a separate, correct implementation.

The lessons are instructive:

1.  **LLMs can identify potential issues but cannot 100% reliably
    determine whether they are actually exploitable.**  The same
    model that flagged the buffer concern also dismissed it in a
    subsequent session.  Static analysis --- including LLM analysis ---
    is necessary but not sufficient for memory safety.

2.  **Fuzzing is an authoritative arbiter.**  The 24 crash inputs
    constituted proof in a way that no amount of code review (human
    or LLM) could match.  Coverage-guided fuzzing explores execution
    paths that reviewers overlook.

3.  **The combination of LLM review and fuzzing is more powerful
    than either alone.**  The LLM narrowed the search space by
    identifying the specific code path and failure mode.  The fuzzer
    proved the vulnerability was real.  Without the LLM's earlier
    flag, the fuzzing campaign might not have been directed at list
    parsing; without the fuzzer, the LLM's retracted concern would
    have remained unresolved.

4.  **Trust but verify.**  LLM recommendations for code changes ---
    especially in security-sensitive code paths --- should be
    validated by fuzzing or formal methods before being considered
    safe.  An LLM that says "this is fine" is not a proof of safety.

### 6.6  Case Study: The [append] Cache Bug

A complementary failure mode emerged from the `[append]` command.
An LLM-designed caching optimization introduced a per-variable
buffer cache to reduce allocation overhead in tight loops.  The
optimization passed all existing tests and showed measurable
improvement on benchmarks.

The bug was subtle: buffer cache entries retained their `nUsed`
state across procedure frame boundaries.  When `[append]` was
called inside a procedure invoked multiple times, each invocation
accumulated stale data from previous calls.  The bug only
manifested in the `appendArgs` procedure (from Eagle's
compatibility library), which is called repeatedly during argument
assembly.  It was discovered through a chain of investigation
starting from unexplained duplicate output in curl tests --- far
from the actual root cause.

The fix was to redesign `[append]` to use the buffer pool as a
pure allocation cache: get a fresh buffer, precompute the total
size (existing value plus all arguments), assemble the result in
one pass, copy to the variable, and return the buffer.  No
persistent per-variable state survives across calls.

The lesson: **LLM-designed optimizations must be tested with
adversarial patterns, not just happy-path benchmarks.**  The
original test suite exercised `[append]` within a single frame.
Only a test that called a proc containing `[append]` multiple
times --- the `appendArgs` pattern --- would have exposed the
cross-frame leakage.  When an LLM proposes a caching optimization,
the review should immediately ask: "what happens when the cache
outlives the scope it was designed for?"

### 6.7  Case Study: Hardening the Memory Allocation Strategy

TH8's memory allocation subsystem underwent a multi-stage
hardening process that illustrates how an architect guides an
LLM agent through a design evolution --- from a naive but
functional starting point to a robust, testable, and
security-hardened system.

**Stage 1: The naive design.**  TH8 initially followed Tcl's
historical pattern: `Th8_Malloc` either returns memory or calls
`xPanic` to abort the process.  This is acceptable for a
standalone interpreter but catastrophic for a sandbox: a
malicious script could crash the host process simply by
requesting a large allocation.  The architect identified this
gap when the `::th8testlib::sandbox` command (which creates a
child interpreter with resource limits) was observed to abort
the entire process when the child's memory limit was exceeded.

**Stage 2: The dual API.**  Following Tcl's own evolution
(`ckalloc` vs. `attemptckalloc`), the architect directed the
LLM to create `Th8_AttemptMalloc` and `Th8_AttemptRealloc`
--- allocation functions that return NULL on failure without
panicking.  The implementation was clean: a shared
`th8MallocCommon` helper with a `bPanic` flag, making the two
APIs differ in exactly one behavior.  The LLM drafted the
implementation; the architect verified the flag semantics and
the alloc-limit check ordering.

**Stage 3: The audit.**  The architect then directed the LLM to
perform a comprehensive audit of all 163 `Th8_Malloc` /
`Th8_Realloc` call sites across 21 source files, categorizing
each as either bounded-internal (safe to panic, e.g.,
`sizeof(Th8_Namespace)`) or script-reachable-unbounded (must
use Attempt, e.g., string data, list buffers, file contents).
The LLM correctly categorized 109 sites as script-reachable and
54 as bounded-internal.  The architect reviewed the
categorizations and identified several edge cases: `Th8_HashNew`
(fixed struct size but called from script paths), bigint bridge
allocations (only reachable if the embedder enables bigint), and
channel creation (reachable via `[file tempname]`).

**Stage 4: The conversion.**  The LLM converted all 109
script-reachable sites to `Th8_AttemptMalloc` in seven parallel
batches, adding NULL checks with appropriate cleanup at each
site.  The architect then directed a second audit of the
remaining `Th8_Malloc` calls, asking: "which of these could be
reached if optional features are enabled in a sandbox?"  This
caught 13 additional sites (hash tables, cache entries, bigint,
crypto, channel, try/catch state) that the first audit
classified as "bounded-internal" but were in fact reachable from
scripts.

**Stage 5: The NULL-path audit.**  Even after conversion, the
architect was not satisfied.  Several remaining `Th8_Malloc`
sites had existing NULL checks that appeared correct but had
subtle issues: namespace creation stored NULL hash pointers in
a registered hash entry (state corruption), 1-byte variable
initialization dereferenced NULL (crash), and `PkgInfo`
allocation assigned NULL to a hash entry without checking
(crash).  The LLM found and fixed 8 risky sites; the architect
identified the root cause patterns.

**Stage 6: `Th8_SetResultStatic`.**  The architect recognized
that calling `Th8_SetResult(interp, "out of memory", ...)` in
an OOM error path is itself an allocation --- and could fail.
The solution: `Th8_SetResultStatic`, which stores a pointer to
a static string literal without copying.  The LLM converted
all ~280 static-string `Th8_SetResult` calls across the entire
codebase, eliminating thousands of unnecessary micro-allocations
and making every error path allocation-free.

**Stage 7: Fault injection platform.**  The architect directed
the creation of `th8_fault.c`, a composable platform wrapper
that intercepts allocation callbacks and returns NULL after a
configurable number of successful allocations.  The fault layer
is dynamically installable via `Th8_FaultInstall` /
`Th8_FaultUninstall` and is gated on
`TH8_ENABLE_FAULT_INJECTION`.  A test command
(`::th8testlib::fault eval`) creates an isolated child
interpreter with `xPanic=NULL`, installs the fault layer,
evaluates a script, captures results, and destroys the child ---
ensuring fault testing never affects the parent interpreter.

**Stage 8: Systematic fault sweep.**  Running `fault eval
{expr {2+2}} -allocFailAfter N` for N from 1 to 25 immediately
found a NULL-dereference crash in `th8BufWrite` --- the internal
buffer growth function did not check the return value of
`th8BufferAlloc` before writing through the buffer pointer.
AddressSanitizer confirmed the fix eliminated all memory safety
violations across the full sweep.  The fault sweep now produces
either a clean error or a correct result at every threshold.

**Stage 9: Stubs and compile-time reporting.**  The fault
injection API functions were missing from the stubs table
preprocessor guards (`tools/mkstubs.tcl`) and from the
compile-time options list (`th8_ctime.c`).  Both were corrected:
stubs entries are now gated on `TH8_ENABLE_FAULT_INJECTION`, and
`Th8_GetCompileOptions` reports the option when active.  A
duplicate `TH8_PLATFORM_COSMOPOLITAN` entry in `th8_ctime.c` was
also removed.

**The architect's role.**  At no point did the LLM independently
decide to create the dual API, to audit the remaining calls, to
check NULL-path correctness, to invent `Th8_SetResultStatic`, or
to build the fault injection infrastructure.  Each stage was
initiated by the architect asking a question or identifying a
gap.  The LLM executed each stage with high accuracy and speed
--- 109 conversions across 21 files in minutes, not hours ---
but the design trajectory was entirely human-driven.  This is
the central lesson of the case study: LLM-assisted development
is architect-led, not agent-autonomous.

### 6.8  Case Study: Policy Callback Consolidation

The unification of TH8's policy callback from two separate
callbacks (`preGetData` and `preEval`) into a single
`Th8_PolicyProc` with a phase bitmask illustrates a different
refactoring pattern: API simplification with semantic
preservation.  The old design had two independent callback slots,
two setter functions, and duplicated state management.  The new
design has one callback that receives a phase argument
(`TH8_PHASE_PRE|TH8_PHASE_READ`, `TH8_PHASE_PRE|TH8_PHASE_EVAL`,
`TH8_PHASE_POST|TH8_PHASE_EVAL`), one setter
(`Th8_SetPolicyCallback`), and unified state.

The LLM drafted the unified callback signature and converted all
call sites, including the test infrastructure.  The critical human
contribution was the design decision itself: recognizing that the
two callbacks were always set together, always shared state, and
always needed coordinated behavior (the `POST|EVAL` phase clearing
the verification flag set by the `PRE|READ` phase).  The LLM
executed the refactoring; the human recognized the opportunity.

### 6.9  Case Study: Platform Callback Uniformity

The platform abstraction layer's 66 callbacks were
standardized to follow a uniform signature convention: every
callback receives `(Th8_Interp *interp, void *pCtx, ...)` as
its first two parameters, where `pCtx` is the platform's opaque
context pointer.  This seemingly mechanical change touched every
platform file (POSIX, Win32, macOS, libc, null-I/O, curl,
mimalloc, cosmopolitan), every call site in the core, and the
stubs table.

The LLM performed the bulk conversion with high accuracy, but the
human caught several cases where the parameter reordering
interacted subtly with existing code: platform initializer structs
that used positional (non-designated) initializers needed careful
reordering, and several callbacks had existing `pCtx` parameters
under different names that needed to be unified.  The lesson
reinforces Section 6.4's observation: the LLM excels at systematic
transformations across many files, but the human must verify each
transformation against the surrounding context.


### 6.10  Case Study: The SplitList Interior-Pointer Bug

The most insidious memory corruption bug discovered during TH8
development illustrates a failure mode that neither human review
nor LLM analysis caught during implementation --- only a different
allocator's runtime assertions exposed it.

`Th8_SplitList` returns two arrays: `azElem` (element string
pointers) and `anElem` (element lengths).  As a memory optimization,
both arrays share a single allocation block:
`[azElem pointers | anElem sizes | NUL-terminated strings]`.
Only `azElem` (the block start) is a valid free target.  `anElem`
is an interior pointer: `&azElem[nCount]`.

Seven call sites across four files called `Th8_Free(interp, anElem)`
after freeing `azElem`, passing the interior pointer to the allocator.
With the system `malloc`, this silently corrupted the heap.  The
corruption manifested unpredictably: sometimes an "oversize string"
panic, sometimes empty results from unrelated operations, sometimes
no visible effect at all.  The bug had existed since the initial
SplitList optimization and survived months of testing.

The bug was discovered when mimalloc was integrated as an optional
high-performance allocator.  Unlike system `malloc`, mimalloc
performs strict size validation on every `free`: `mi_usable_size`
checks that the pointer is a valid block start.  The interior pointer
failed this check, triggering an immediate assertion:

```
th8sh: mi_assert_fail: mi_usable_size(p) > 0
```

The backtrace pointed directly to the `Th8_Free(interp, anElem)`
call.  From there, the fix was mechanical: remove all seven interior-
pointer free calls.  The affected files were `th8_sqlite3.c` (KV
SET2/UNSET2), `th8_env.c` (SET2/UNSET2, Win32 paths), `th8_filesystems.c`
(glob filtering), and `th8_io.c` (close all channels).

The lessons are complementary to Section 6.5:

1.  **Strict allocators find bugs that permissive ones hide.**
    System `malloc` silently tolerates many categories of invalid
    free (double-free, interior pointer, wrong-size-class).
    Switching to mimalloc (or ASan, or Valgrind) can expose latent
    bugs that have been invisibly corrupting memory for months.

2.  **Single-block optimizations create non-obvious invariants.**
    The optimization of packing `azElem` and `anElem` into one
    allocation saved a `malloc` call per list split --- a meaningful
    performance win --- but created the invariant "only `azElem` is
    freeable."  This invariant was not documented, and the LLM
    (which drafted several of the affected call sites) did not
    recognize it.  Every call site that received `anElem` naturally
    assumed it was an independent allocation.

3.  **The LLM's pattern was consistent but wrong.**  All seven
    incorrect `Th8_Free(interp, anElem)` calls followed the same
    pattern: split a list, use both arrays, free both arrays.  The
    LLM applied this pattern consistently across files, which is
    exactly what you want for correct patterns.  When the pattern
    itself is wrong, consistency amplifies the bug.

4.  **Minimal reproducers are essential.**  The initial report was
    a crash in KV SET2 with `uplevel 1` and `catch`.  Multiple
    hypotheses were explored (cache corruption, hash collision,
    decryption error, SplitList cache staleness) before the
    mimalloc assertion pinpointed the exact cause.  The debugging
    took over two hours; the fix took two minutes.

### 6.11  Case Study: Annotation Scanner Precision Bugs

TH8's script annotation system (`<<notBefore:...>>`,
`<<notAfter:...>>`) extracts temporal access control metadata from
Tcl comments.  A series of three interacting bugs in the annotation
scanner demonstrates how small off-by-one errors in string parsing
can chain together to produce a completely non-functional feature
that nonetheless passes syntactic validation.

**Bug 1: Comment prefix change without offset update.**  The scanner
was changed to require annotations in comments only, looking for
`# <<` instead of bare `<<`.  The content start moved from position
`i+2` to `i+4`, but the content length calculation still used
`j-(i+2)` instead of `j-(i+4)`.  Result: every extracted timestamp
was two bytes too long (e.g., `2026_04_24T12_00_00Z>>` instead of
`2026_04_24T12_00_00Z`).  The timestamp parser's `n != 20` strict
length check then rejected every valid annotation.

**Bug 2: Leap second validation.**  The timestamp parser validated
`second > 59` as invalid, but UTC leap seconds are valid at `:60`.
The correct check is `second > 60`.  This was independently caught
by the human architect from the Harpy specification.

**Bug 3: Save/restore ordering in `Th8_EvalFile`.**  After fixing
bugs 1 and 2, annotations were parsed correctly but their values
disappeared after `source` returned.  The root cause: `Th8_EvalFile`
saved `::th8_security` AFTER calling `Th8_GetData`, so the saved
state captured "none" for annotation values.  The policy callback
correctly populated `notBefore`/`notAfter` during verification, but
the restore on return overwrote them with "none".  The fix required
separating the save into two parts: saving the security array BEFORE
`Th8_GetData` (to preserve the outer file's `dataName` stacking),
and separately snapshotting annotation values AFTER `Th8_GetData`,
then re-applying the snapshot after restore.

The chain of bugs is instructive because each was individually
trivial (a constant off by 2, a boundary off by 1, an ordering
dependency) but together they rendered the entire annotation system
non-functional.  The LLM introduced all three bugs during
implementation, and the LLM was also instrumental in diagnosing
them --- but only with significant human guidance at each step.  The
human added trace output that showed the correct timestamp string
being extracted but still failing validation, which narrowed the
search to the length check.  Without that observation, the LLM's
initial hypothesis (incorrect prefix matching) would have led to a
time-consuming dead end.

### 6.12  Case Study: Meta-Header Refactoring and Layer Discipline

A late-stage refactoring of TH8's system header includes into
centralized meta-headers illustrates both the LLM's strength at
systematic transformation and the human's irreplaceable role in
enforcing architectural principles.

TH8 source files individually included system headers (`<string.h>`,
`<sys/mman.h>`, `<windows.h>`, etc.) with per-file feature test
macros and platform guards.  Linux compilation failures occurred
because feature test macros (`_GNU_SOURCE`, `_POSIX_C_SOURCE`) were
missing or inconsistent across files.  The architect directed the
creation of five centralized meta-headers:

-   `th8_meta_defs.h` --- standard feature test macros
-   `th8_meta_libc.h` --- standard C headers
-   `th8_meta_posix.h` --- POSIX headers
-   `th8_meta_macos.h` --- macOS-specific headers
-   `th8_meta_msvc.h` --- MSVC CRT headers
-   `th8_meta_win32.h` --- Win32 API headers

The LLM drafted the meta-headers and converted 15 source files in
a single pass.  Three rounds of human correction followed:

1.  **Header classification.**  The LLM initially placed
    `<malloc/malloc.h>` (macOS) and `<malloc.h>` (Linux) in the
    libc meta-header.  The human pointed out that these are
    platform-specific, not standard C.  They were moved to
    `th8_meta_macos.h` and `th8_meta_posix.h` respectively.

2.  **MSVC vs. Win32 distinction.**  The LLM grouped `<io.h>` and
    `<malloc.h>` with `<windows.h>`.  The human recognized these as
    MSVC CRT headers, not Win32 API headers, leading to the creation
    of a separate `th8_meta_msvc.h`.

3.  **Transitive include removal.**  The LLM's initial design had
    `th8_meta_posix.h` include `th8_meta_libc.h`, and
    `th8_meta_win32.h` include `th8_meta_msvc.h`.  The human
    directed that meta-headers must NOT include each other: every
    `.c` file must list the meta-headers it needs explicitly, in
    logical order (libc first, then platform-specific).  This
    decision prioritizes long-term readability over convenience ---
    a developer reading any `.c` file can see exactly which header
    categories it depends on without tracing transitive includes.

The refactoring touched every `.c` file in the source tree and
eliminated all direct system includes from implementation files.
The verification was simple: `grep` for any `#include <` in `.c`
files that was not from the meta-headers.  The result was empty.

The case study reinforces the "layer discipline" theme: the LLM
performs systematic transformations efficiently, but the human
architect must define and enforce the boundaries between layers.
Without human intervention, the meta-headers would have had a
reasonable but suboptimal design (transitive includes, wrong layer
assignments).  With human correction, the result is a clean
architecture that prevents the original Linux compilation problem
by construction.

### 6.13  Case Study: Defeating Your Own Safety Net

A late audit of TH8's `TH8_ALLOC_*` overflow-safe allocation macros
revealed a recurring anti-pattern that is instructive about how a
well-intentioned safety net can be silently defeated by the very
people it protects.

The macros (`TH8_ALLOC`, `TH8_ALLOC_STR`, `TH8_ALLOC_MUL`,
`TH8_ALLOC_ADD`, `TH8_ALLOC_MULADD`) perform overflow-checked
arithmetic before calling the underlying allocator.  The contract
is that any size computation reaching the macro is validated
against `size_t` overflow.  But the contract only protects the
arithmetic *inside* the macro --- inner expressions in the macro's
arguments are plain C `size_t` math that wraps silently to 0 on
overflow.  Twenty sites across the codebase had patterns like:

```c
TH8_ALLOC(interp, n + 1)                    /* + 1 unchecked */
TH8_ALLOC_ADD(interp, 2, nTail + 1)         /* nTail + 1 unchecked */
TH8_ALLOC_MUL(interp, n + 1, sizeof(WCHAR)) /* n + 1 unchecked */
nA = nE * sizeof(char *) + nE * sizeof(size_t) + nST;
TH8_ALLOC(interp, nA);                      /* 4 unchecked steps */
```

In each case, an attacker (or merely a very large input) that drove
the inner expression to wrap would yield a size of 0 or a tiny
post-wrap value.  The macro would dutifully validate the wrapped
value, allocate it, and return success --- handing the caller a
buffer far smaller than they intended to write into.  Classic heap
buffer overflow.

The fix had three parts.  First, a comprehensive audit identified
every `TH8_ALLOC_*` call whose arguments contained `+` or `*`, plus
every pre-computed multi-term size assigned to a variable and then
passed to a single `TH8_ALLOC`.  Second, four new public APIs filled
the gaps where existing macros could not express the required
arithmetic safely: `TH8_ALLOC_STR_ADD`, `TH8_ALLOC_STR_MUL`,
`TH8_ALLOC_MUL_ADD2`, and the underlying `Th8_SafeMul` /
`Th8_SafeAdd` primitives for callers whose size math doesn't fit any
macro.  Third, an "anti-pattern catalog" was added to the API
specification, documenting each unsafe form alongside its canonical
safe replacement, so future contributors are pointed at the right
construct during review.

The lessons are general:

1.  **A safe wrapper does not make its caller safe.**  The macro is
    one well-defined point of validation; everything before it is
    plain C.  The temptation to write `TH8_ALLOC(interp, n + 1)` is
    enormous because it looks safe --- you're calling the safe
    allocator.  The cure is to provide named constructs for every
    common pattern (NUL terminator, prefix + tail, packed records)
    so the obvious thing to write is also the safe thing.

2.  **Audit at the syntax level, not the intent level.**  No human
    reviewing 20 patches one at a time would have caught each
    `+ 1` --- they look harmless.  A single `grep` for `TH8_ALLOC.*[+*]`
    found them all in seconds.  Mechanical syntactic audits scale
    where review of intent does not.

3.  **The anti-pattern is more valuable than the pattern.**  The
    catalog entry that says "do NOT write `TH8_ALLOC(interp, n + 1)`,
    write `TH8_ALLOC_STR(interp, n)` instead" has caught more bugs
    in code review than any positive style guide entry.

### 6.14  Case Study: Reference Counting and the Asymmetric Test Suite

A regression in the load-tracking test (`load-3.6`: "unload of an
already-unloaded library is an error") illustrated how subtle
shared-state asymmetries in test infrastructure can produce
intermittent failures that look like implementation bugs.

The test loads the testlib, unloads it, and verifies a third unload
fails.  Run standalone, the test passed reliably.  Run as part of
the full test suite, it failed every time --- the third unload
*succeeded*.

The proximate cause was that TH8's `[load]` is reference-counted at
the OS level (matching POSIX `dlopen` and Win32 `LoadLibraryA`
semantics, where each load increments a refcount and each unload
decrements).  The test suite's per-file `prologue.tcl` calls
`testLoadLib`, which always issues a fresh `[load]`.  Each test
file thus added one to the testlib's load count.  By the time
`load.tcl` ran, the count was 30+.  The test's three unloads
brought the count to 27, not zero, so the third "should fail"
unload succeeded.

The fix had two layers.  At the test infrastructure level, the
epilogue must undo what the prologue did: a single `testUnloadLib`
call at the end of every test file restored symmetry, capping the
count at 1 throughout the suite.  At the API level, the load
tracking was changed from "one entry per `[load]` call" to true
reference counting --- multiple `[load]` calls of the same library
now bump a counter rather than appending duplicate entries, and
`[info loaded]` returns `{name refCount}` pairs instead of a flat
list with duplicates.  The result is that the API observably
matches OS conventions, the test suite stays balanced, and any
future reference-count bug becomes immediately visible via
`[info loaded]`.

The lessons:

1.  **Asymmetric infrastructure breeds asymmetric bugs.**  A
    prologue without a matching epilogue creates a slow accumulation
    that fails late, far from the file that introduced it.  The
    rule is: every effect a per-file prologue produces must be
    undone by the corresponding epilogue.  Tests are most
    debuggable when each file is self-zeroing.

2.  **Match the underlying semantics.**  TH8's load tracking
    *appeared* to be reference counted (multiple `[load]` calls
    each succeeded) but was actually multiplicity-counted (each
    call appended a new tracking entry).  When the user-visible
    behavior diverges from the OS semantic, scripts are surprised.
    Aligning TH8's tracking with OS behavior eliminated the
    divergence and let `[info loaded]` become a real
    diagnostic tool.

3.  **Resource-mutation tracking is only as strong as its weakest
    axis.**  TH8's per-test resource-mutation tracker covered nine
    resource types (procs, vars, commands, namespaces, channels,
    packages, functions, expansions, breakpoints) but not loaded
    libraries.  Adding `[info loaded]` as the tenth tracked type
    immediately surfaced existing leakage that earlier had only
    manifested as the load-3.6 failure.  Each "blind spot" in
    accounting eventually becomes a place where bugs hide.

### 6.15  Case Study: Defense in Depth for Sensitive Plaintext

The path of a decrypted secure-variable plaintext to the
interpreter result evolved through three iterations, each one
exposed by a question of the form "but what about the gap
*here*?"  The result is a system that keeps decrypted plaintext
out of pageable memory, off swap files, out of core dumps, and
unable to be detached and copied by careless C callers --- all
properties that the first version did not have.

**Iteration 1 (initial):** A `[secure]` variable read decrypted
into a regular `Th8_Malloc`'d heap buffer, copied the plaintext
into the interpreter result via `Th8_SetResult`, set a sensitive
flag, and securely zeroed the temporary buffer.  This protected
*nothing in particular*: the plaintext lived in pageable heap for
the entire duration of the read, then in the result buffer (also
pageable heap) until the next result change zeroed it.  The
sensitive flag's only effect was to trigger the secure-zero on
overwrite.

**Iteration 2 (mlock'd result region):** A new
`Th8_SetResultSensitive` API copied the plaintext into a per-
interpreter `Th8_ProtectedRegion` --- one OS page, mlock'd against
swap, flanked by `PROT_NONE` guard pages, marked
`MADV_DONTDUMP`/`MADV_WIPEONFORK` on Linux.  The result now lived
in protected memory.  But the *temporary* `zPlain` buffer between
decrypt and `Th8_SetResultSensitive` was still in regular heap.
The user noticed: "Am I wrong or should the buffer used by
`th8SecureDecrypt` actually reside in a protected region?"  Yes ---
the entire reason for the protected region was undermined by the
unprotected staging buffer feeding it.  Refactoring `th8SecureDecrypt`
to write directly into a caller-provided `Th8_ProtectedRegion`
eliminated the staging buffer entirely.

**Iteration 3 (single shared region):** The refactor introduced
*two* protected regions: one for sensitive results, one as scratch
for `th8SecureSave`'s re-encryption flow.  The user noticed again:
"Out of curiosity, why have `pProtectedResult` *and*
`pProtectedScratch`?  Can't decrypt directly use `pProtectedResult`?"
Yes --- two regions had been a reflexive over-engineering, born of
treating the result region as result-only.  Once you `Th8_ClearResult`
before reusing the region for unrelated work, one region is
sufficient; this is exactly what `th8SecureSave` does.  The two
regions were collapsed into one.

The final architecture: a single per-interpreter
`Th8_ProtectedRegion`, lazy-allocated on first sensitive use,
freed only by `Th8_DeleteInterp`.  Three internal helpers
(`th8GetProtectedResultRegion`, `th8FinalizeSensitiveResult`,
the parameterized `th8SecureDecrypt`) ensure that decrypted
plaintext is written directly into protected memory, becomes the
live sensitive result with no copy, and is automatically
secure-zeroed when overwritten.  `Th8_TakeResult` refuses to
detach a sensitive result, blocking the most obvious C-level
escape path.

This case study illustrates two lessons that compound:

1.  **Defense in depth has to be continuous, not punctuated.**
    Protecting the destination buffer matters only if the bytes
    don't transit unprotected memory en route.  Each iteration
    closed one gap; only the third version actually delivered the
    property the architecture was meant to provide.  The right
    mental model is "follow the plaintext byte-by-byte from
    ciphertext source to script consumer; flag every place it
    lives, however briefly."

2.  **Reflexive over-engineering hides under "more is safer."**
    Two protected regions felt safer than one --- "the result
    can't accidentally clobber save's scratch."  But the second
    region added complexity, an additional mlock'd page per
    interpreter, and a second lazy-allocation site to maintain,
    while the supposed isolation was illusory: the same code path
    serializes all secure-variable operations within an interpreter
    anyway.  The user's question forced the simplification.  The
    instinct to add belt-and-suspenders is healthy in security
    code, but each duplicate must justify itself against the
    cost of having two things that must agree.

The user's two questions ("shouldn't the decrypt buffer be
protected?" and "why two regions?") in this case study are
worth examining as a meta-lesson: the most valuable code review
intervention is often not "this is wrong" but "are you sure this
is necessary?"  Both questions led directly to designs that were
simultaneously simpler and stronger than what they replaced.


### 6.16  Case Study: Codifying the Anti-Pattern Catalog

Section 6.13 described a one-time manual audit that found twenty
unchecked size arithmetic sites underneath the `TH8_ALLOC_*`
macros.  That audit produced a textual anti-pattern catalog in the
API specification but no permanent enforcement: nothing prevented
new code from re-introducing the same patterns.  The natural sequel
was a static checker that would refuse the build if any of the
catalogued anti-patterns reappeared, and would accumulate further
project-specific rules over time.

`tools/audit_patterns.tcl` (~650 lines, mostly comments) is that
checker.  It is a regex-rule registry: each rule is a name, a Tcl
ARE-flavor regex, a description, a fix suggestion, and optional
file-glob include/exclude lists.  Lines are scanned post comment
and string-literal stripping (a hand-rolled state machine modelled
on Fossil's `codecheck1.c` token-classifier) so a literal `/* foo
* bar */` cannot false-flag as multiplication.  A trailing in-line
marker `/* AUDIT-OK[<rule-name>]: <reason> */` suppresses a
specific rule on a specific line; multiple rules can be listed
comma-separated.  Make wires the tool to a `make audit` target
that exits non-zero on any violation, suitable for CI gating.

The first rule was `bare-size-multiply`, in three sub-variants
(explicit `(size_t)` cast adjacent to `*`, identifier with TH8's
`n<UpperCase>` size-naming convention adjacent to `*`, and
multiply inside a libc `(m|c|re)alloc` argument).  Adding this
single rule and running it across the source caught **three real
bugs** that section 6.13's manual sweep had missed: a
multi-component cache-block size in `th8_cache.c`, a vector
realloc-grow in `th8_core.c`, and a procedure-table allocation in
`th8_procedures.c`.  Each was an unchecked `nFoo * sizeof(Bar)`
that would have wrapped silently on a sufficiently large input.
All three were fixed with the existing `TH8_SAFE_MUL_SIZE` /
`TH8_SAFE_ADD_SIZE` primitives.

A second pass added rules for direct-libc bypass of the platform
layer (`malloc`, `calloc`, `realloc`, `memcpy`, `memmove`, `memset`,
`memcmp`, `strlen`, `strcmp`, `strchr`, `vsnprintf` etc.).  The
naive form fired 183 times --- many in legitimate boundary code:
the libc bridge file itself, the platform layers, the SQLite plugin
glue (whose foreign API uses libc-shaped strings), the shell entry
point, the test harness.  Adding a per-rule `exclude_globs` list
that names exactly those bridge files cut the count to 20.  Of
those 20, eight were the bare-multiply call sites already counted
above, eleven were either bridge-defining macros (legitimate by
their nature, marked `AUDIT-OK`) or upstream-bounded multiplies
(marked `AUDIT-OK` with the upstream invariant cited), and one was
a real direct `memcpy` in a non-boundary plugin that had simply
been missed during the manual audit.  Routing it through
`Th8_Memcpy` was a one-line change.

A third pass codified the project's "use the macros, not the
primitives" rule: bare calls to `Th8_Malloc`, `Th8_Realloc`,
`Th8_AttemptMalloc`, `Th8_AttemptRealloc` were forbidden in core
code in favour of `TH8_ALLOC` / `TH8_ALLOC_*` and (newly added)
`TH8_REALLOC` / `TH8_ATTEMPT_REALLOC`.  The macro family wraps the
primitives in calls that capture `__FILE__` and `__LINE__` for OOM
diagnostics, so a violation is not just a stylistic nit but a real
loss of debugging information when an allocation fails on a
production build.  The new realloc macros required two small
backing functions (`Th8_SafeRealloc` / `Th8_SafeAttemptRealloc`),
mirroring the existing `Th8_SafeAlloc` shape.

A final pass added the lexical-style rules with no boundary-file
exemption: `gets()` is banned outright (removed from C11
entirely), and so are `strcpy` / `strcat` (no length parameter,
the canonical buffer-overflow primitive).  The latter rule
caught one further violation in `th8_posix.c` --- an upstream
`if (strlen(x) < sizeof(buf)) strcpy(buf, x);` whose explicit
length check made the call functionally safe but not auditably
safe.  It was rewritten using `memcpy(buf, x, strlen(x) + 1)`
with the length cached, which is both faster and self-documenting.

The current rule set has 39 rules across seven categories:
size-multiply overflow safety, libc bypass detection (including
stdio, time, threading, signal, env-access, filesystem, and the
core memory ops), public-header purity (`<windows.h>` must not
appear in the public header chain), naming and header hygiene,
allocation-macro misuse, platform-ABI version sanity, and the
CERT-C / CWE-Top-25 banned-function family (`gets`, `strcpy`,
`strcat`, `strncpy`/`strncat` with their NUL-termination
pitfalls, `rand`/`srand` for security, `tmpnam`/`tempnam`/
`mktemp` and other TOCTOU-vulnerable temp-file APIs, `alloca`
for unbounded stack growth, `scanf`-family for buffer-overflow
parsing, `system`/`popen`/`exec*` for command injection, and the
classic `if (x = y)` assignment-in-conditional typo).  The full
source plus all platform and plugin files (~85 files) audits in
well under a second.

Two specific lessons emerged:

1.  **The codification sequel is mechanical, but the bug catch is
    not.**  Section 6.13's manual audit was a one-time
    intervention; no human would ever do that audit again.
    Codifying the same audit into a permanent check has the
    obvious benefit of preventing regression, but the surprising
    side-effect was finding **new** instances of the very pattern
    the manual audit was meant to eradicate --- in code paths the
    manual audit's ad-hoc grep had missed.  The codified rule's
    coverage was strictly broader because the codification forced
    the rule to be precise; the manual audit's "I'll know it when
    I see it" is necessarily incomplete.

2.  **The signal-to-noise ratio is the load-bearing engineering
    decision.**  A rule that fires 183 times on a clean codebase
    will be ignored within a week.  The discipline of either
    (a) refining the regex until false positives drop into single
    digits, (b) adding a precise exclude-glob list to cover
    legitimate boundary code, or (c) writing an `AUDIT-OK` marker
    with a specific reason is what makes the difference between
    a tool that catches bugs and a tool that everyone has
    learned to ignore.  The marker discipline is particularly
    valuable: a one-line `/* AUDIT-OK[rule-name]: reason */` is
    self-documenting evidence that *this site was reviewed and
    deliberately accepted*, which is exactly the audit trail a
    static checker is supposed to produce.

A natural meta-finding falls out of the survey that drove this
case study.  Of TH8's 1,151 R-marker'd normative requirements
(834 in the language standard, 260 in the language extensions,
248 in the C API specification), an LLM-assisted survey
identified roughly 10 candidate rules that
might be codifiable into a regex-on-line audit tool.  Of those 10,
only 4 turned out to be genuinely lexical-static-checkable; the
remaining 6 required either flow analysis ("does this callback
guard against absolute paths?"), runtime invariants ("does the
SQLite KV op survive process termination?"), or human judgment
("is the wrapper signature correct given the entire call graph?")
to verify.  The mechanically-enforceable subset is the one this
tool addresses; the rest are the legitimate domain of the test
suite, fuzz harnesses, and human review.  A specification's value
is partly measured by how much of it survives this projection
into mechanical enforcement, and TH8's specification is unusually
testable on that axis: the language semantics live in the test
suite, the security invariants live in the fault-injection /
fuzz harnesses, and a meaningful slice of the coding-style
discipline now lives in a 650-line tcl scanner.

### 6.17  Case Study: Failure Modes in Synthetic Test Generation

A specification with 1,151 normative requirements is only as
strong as the test suite that verifies it.  TH8's authoring
process tracks coverage with `tools/mkreq.tcl --check-tests`,
which reports the count of orphan markers (test references
pointing at no-longer-present standard text) and the count of
covered markers (markers cited by at least one test).  At the
2026-08-07 cutoff, that tool reports zero orphans and 1,134 of
1,151 markers covered (98.52%) -- the residual 17 are
concentrated in C-API and platform requirements that need
specialized C-level test infrastructure.  The headline metric
looks healthy.

A post-hoc audit told a different story.  Of the 133
R-marker'd tests in self-declared "Conformance Test File"
synthetic test files (`coverage*.tcl` -- written specifically
to fill marker-coverage gaps), approximately 30 (~23%) had a
non-trivial gap between the requirement text the marker pinned
and the property the test body actually verified.  This case
study catalogues the ten distinct failure modes observed in
specification-style tests, three more in code-coverage-style
tests, and three cross-cutting modes that apply to the suite
as a whole.  The framing is Goodhart's Law applied to test
suites: when "every R-marker has at least one citing test"
became the metric, the path of least resistance to satisfying
it sacrificed the underlying property the metric was a proxy
for.

The audit methodology was deliberately cheap.  A 60-line
Python extractor walked the test corpus and produced, for each
`runTest`-wrapped test, a tuple `(file, line, test-name,
R-marker, requirement-text, body, expected-result)`.  A
heuristic flagger applied a small set of risk-keyword regexes
to the requirement text ("sufficient", "at least", "beyond",
"propagates", "during", "clears result", "stable sort", "UTF-8",
"sleeps for", "monotonic", "rescans", "invokes when") and
reported tests whose body did not contain corresponding
language.  This produced 60 candidates across 91 files; hand
inspection confirmed 30 as real findings and explained the
rest as either heuristic false positives or genuinely-passing
tests where the body and requirement just happened to share no
keywords.  The 133 tests in the synthetic conformance files
were also hand-read end-to-end as a control.  Total elapsed
audit time was approximately a single working day, including
writing this case study.

#### 6.17.1  The two-category contract

A foundational distinction worth making explicit before
enumerating failure modes: TH8's `tests/` tree contains two
kinds of tests with different purposes and different
correctness criteria, and conflating them is itself a failure
mode.

**Specification tests** (the "Conformance Test File" files)
verify that the implementation behaves as a normative
requirement in the standard, the language extensions, or the
C API specification claims.  Every test cites at least one
R-marker; the body's pass-criterion must correspond directly
to the property the marker pins.  Equivalently, the test is a
Boolean function on the space of implementations, true exactly
on those that satisfy the requirement.  A weak specification
test is one that returns true on implementations that violate
the requirement.

**Code-coverage tests** (the "coverage-driven" files,
explicitly headered as such: `coverage4.tcl`, `coverage5.tcl`,
`coverage7.tcl`, `coverage_parser.tcl`, `coverage_glob.tcl`,
`coverage_expr_*.tcl`) drive execution through corner-case
branches in the C code that are rarely or never reached by
specification tests.  These tests need not cite an R-marker;
the pass-criterion is "the implementation did not crash, leak,
or produce a malformed result on this corner case."  Their
purpose is to catch faults in the implementation's internal
handling of unusual inputs, error paths, recovery sequences,
and state-machine edges -- regardless of whether the
implementation's documented contract has anything to say about
them.

A test in the wrong category is a category error: a coverage
test mistakenly cited under an R-marker dilutes the
specification's verification base; a specification test
written like a coverage test (no R-marker, no documented
property in scope) hides outside the conformance audit.  TH8's
current corpus has at least one canonical instance of this
mismatch (`coverage6.tcl` self-identifies as "Conformance Test
File" but its 36 escape-sequence-corner-case tests have no
R-markers and read more naturally as code coverage), and it
shows up below as failure mode 3.A.

The remaining failure modes are catalogued in the categories
they apply to.

#### 6.17.2  Specification-test failure modes (ten)

**(1) Proxy-instead-of-property.**  The body verifies a
measurable consequence of the requirement instead of the
requirement itself.  The consequence is necessary but not
sufficient.  Canonical example: requirement R-55223-15762
*"clock seconds returns a wide integer sufficient to represent
dates beyond 2038"*, body `expr {[clock seconds] > 0}`.  A
32-bit `time_t` system that's destined to fail in 2038 returns
a positive integer today (2026); the test passes for the wrong
reason.  Other corpus instances: "since the Unix epoch"
verified by `> 1000000`, "after sleeps for N ms" only checking
elapsed `< 5 seconds`, "errorInfo contains a stack trace"
verified by `string length > 0`.  Root cause: the property as
stated is hard to test directly (you cannot observe a 64-bit
time width in 2026 just by calling `clock seconds`); the
author falls back to whatever visible behavior happens to be
greppable.  Prevention: when the requirement makes a capacity,
timing, range, or structural claim, the body must include an
explicit probe of that property -- if no such probe exists at
script level, add one to the test surface (TH8's
`::th8testlib::*` namespace is the canonical home).  Detection:
flag any requirement text containing "sufficient", "at least",
"beyond", "no more than", "of length", "of size", "of width"
where the body lacks a corresponding inequality, range, or
width assertion.

**(2) Body-unrelated-to-requirement.**  The body does not
even attempt to test the requirement; it asserts an unrelated
and trivially-passable property.  This is the most damning
class because the test reads as a green check while verifying
nothing relevant.  Canonical example: requirement R-21609-56922
*"shall interpret source text as a sequence of bytes encoded
in UTF-8"*, body `string length "abc"` returning 3.  The test
passes identically on a system that interprets source as
ASCII, Latin-1, UTF-16, or any encoding where "abc" is three
of anything.  Other corpus instances: a test for
`Th8_GetMemPlatform` returning non-NULL, verified by
`expr {[llength [info plugins]] > 0}` -- two probes with no
relationship.  Root cause: the property is at the C-API level
and the script has no script-level handle on it; the author
substitutes any script-visible artifact that happens to be a
downstream observable, even though the observable does not
depend on the property.  Prevention: if the requirement is at
the C-API level, the test must use a `::th8testlib::*` helper
that directly probes the C-level state; if no such helper
exists, add one before writing the test.  Generic "the interp
is functional" probes (`info plugins`, `info commands`) do not
qualify.

**(3) Multi-clause-partial.**  The requirement makes two or
more independent claims connected by *or*, *and*, *when*,
*while*, or *until*; the body verifies one and silently drops
the others.  Canonical example: R-32039-29681 *"package
unknown sets or queries the script invoked when package
require cannot find a package"* -- body verifies set-and-query,
but the "invoked when require fails" clause is uncovered.
Similar instances: "ifneeded registers a script that, when
evaluated, provides the version" (registration tested,
evaluation untested); "source returns the **last** command
result" (returns SOMETHING tested, "last" untested); "eval
**concatenates** its arguments... and evaluates" (evaluation
tested, concat-like behavior untested).  Root cause: the
author reads the requirement, picks the easiest clause to
test, and ships.  Prevention: parse each requirement into
atomic clauses (each with one verb and one object) before
writing the body; either cover all clauses in one body or
write multiple sibling tests citing the same R-marker.
Detection: scan requirement text for connectives, count the
verbs, and require the body to contain a comparable number of
distinct assertions.

**(4) Wrong-side-of-conditional.**  The requirement specifies
behavior in a particular state ("when X", "in mode Y", "on
success"); the body exercises a different state and asserts
*its* behavior, not the documented one.  Canonical example:
requirement R-61890-36314 *"close returns empty string"* (a
success-path requirement), body
`catch {close nonexistent_channel} msg; expr {$msg ne ""}` --
verifies the **error** path produces a non-empty message.
Opposite of the documented success behavior.  Root cause: the
easy-to-write test exercises whichever side of the conditional
is reachable in the author's current build; a success-path
test in a fault-injection-only build becomes an error-path
test by default.  Prevention: the body must establish the
precondition before invoking the operation, or the test must
gate on `-constraints { ... }` excluding builds where the
precondition fails.  Bare invocation without precondition
setup is a red flag.  Detection: if the requirement begins
with "when X" / "while X" / "if X", the body must contain a
setup step that establishes X, or the test must declare a
constraint gating on X.

**(5) Property-unobservable-on-chosen-input.**  The property
is real, but the input chosen for the test cannot distinguish
a correct implementation from a broken one.  Canonical
example: requirement R-56935-37465 *"lsort is a stable sort"*,
body `lsort {c c c}` returning `c c c`.  All elements are
byte-identical; stable and unstable sorts produce the same
output.  The test demonstrates "lsort doesn't shuffle
identical input," a property strictly weaker than stability.
Compare `liststring-1.19`, which uses `{ {1 a} {1 b} }` paired
with `lsort -index 0`: this DOES distinguish stable vs.
unstable.  Root cause: the requirement is about a property of
ordering (stability, monotonicity, well-foundedness,
determinism) that is only visible when the input contains
elements that are *equal under the comparator* but
*distinguishable* under direct inspection.  Prevention: for
any property-of-ordering requirement, the input must contain
at least two elements that compare equal but are
distinguishable -- pairs of (sort-key, tag) are the canonical
shape.  Detection: any sort/order test where the input is
`{x x x}` or trivially-distinct elements needs hand
inspection.

**(6) Unconditional-pass-for-conditional-requirement.**  The
requirement is conditional on a build mode or runtime state;
the body's pass-criterion holds in *all* modes, defeating the
build-mode discrimination.  Canonical example: requirement
R-59209-30085 *"random without cryptography returns error"*,
body `set rc [catch {expr {random()}} msg]; expr {$rc == 0
|| $rc == 1}`.  Passes in all builds.  The "without
cryptography" condition is not enforced by the body or by a
constraint.  Root cause: the author wants the test to pass on
their own machine regardless of build configuration, and
accepts all outcomes -- the test no broken implementation can
fail.  Prevention: build-mode-conditional requirements must
gate on a `-constraints {mode}` clause, and the body's
pass-criterion must hold *only* when that mode is in effect.
Detection: any test whose `-result` is a disjunction of
mutually exclusive outcomes (`{0 1}` shape, or
`expr {$rc == X || $rc == Y}` with X != Y) is presumptively
pass-anyway unless the disjunction is genuinely inherent in
the requirement.

**(7) Constraint-equals-body.**  The constraint expression
that gates the test is the same expression the body asserts.
The constraint prevents the test from running in any
environment where it would meaningfully exercise the
implementation; the test can only ever pass-or-skip, never
fail.  Canonical example: requirement R-19715-59368 *"Integer
arithmetic that would overflow the implementation's integer
range shall produce an error when overflow checking is
enabled"*; constraint `testConstraint overflow_check
[expr {[catch {expr {0x7FFFFFFFFFFFFFFF * 2}}] == 1}]`; body
`catch {expr {0x7FFFFFFFFFFFFFFF * 2}}` (expecting result 1).
Under TH8's default-bigint configuration, the multiply
succeeds (returns an arbitrary-precision result), the
constraint evaluates to false, and the test is skipped.  Under
any hypothetical non-bigint build where the constraint were
true, the body would necessarily pass because the constraint
is *the same expression*.  The test cannot fail by
construction.  Worse, no positive test exists to verify what
TH8 actually does with `0x7FFFFFFFFFFFFFFF * 2` in the bigint
configuration: the skipped state is a clean "not applicable",
but a reader of the test output has no signal that the
implementation's actual behavior is verified somewhere.  Root
cause: the author wanted to skip the test in builds where the
requirement isn't applicable and reused the most convenient
predicate -- which happens to be the test's own pass-criterion.
Prevention: the constraint must probe a *property of the
build* (a feature flag, a present command, a platform
capability), not the test's own outcome.  Detection: for each
test, compare its constraints' defining expressions to its
body; any expression appearing in both is a tautological gate.

**(8) Loop-variable-instead-of-command-result.**  The
requirement is about the COMMAND's return value (`for`,
`while`, `foreach` collapsed result), but the body asserts
something about a loop-internal variable that the command is
not specified to return.  Canonical example: requirement
R-61603-53175 *"break within for clears result to empty
string"*; weak body
```tcl
for {set i 0} {$i < 10} {incr i} { break }
expr {$i == 0}                 ;# tests $i, NOT for's result
```
The strong body, used in the feature-organic `for-8.x` tests,
is `set x [for ... break]; set x` paired with `-result {}`,
which directly verifies the for command's collapsed result.
Five tests across `coverage8.tcl` Section 1 use the loop-
variable shape with the same R-markers as the feature-organic
versions, making them simultaneously redundant and weak.  Root
cause: the loop variable is a comfortable artifact to assert
against; the command's return value requires the
`set x [...]` boilerplate.  Description text says "result" but
the body interprets "result" loosely.  Prevention: when the
requirement is about a command's return value, the body's
structure must be `set x [<command>]; $x` (or the command
must be the last expression in the body, with `-result`
matching).  Detection: for any test whose requirement contains
"return", "returns", "result", or "produces", the body must
end in a command-substitution-and-assert pattern OR the
command itself must be the body's final expression.

**(9) Comment-admits-the-gap.**  The body contains a comment
that explicitly acknowledges the test isn't actually verifying
what it claims; the test is filed against the R-marker anyway.
Canonical example: a test for the memory-recovery callback
`xNeedMemory` whose body comment reads *"This exercises the
path where xNeedMemory **might** be called"*.  "Might be
called" is a probabilistic hope, not a verification.  The test
passes regardless of whether `xNeedMemory` actually fires.
Other instances: comments saying *"We cannot easily verify
newline output in the test framework, but..."*; *"After break,
the interp result should be empty (but we test something
else)"*.  Root cause: the author hit a wall, knew it,
documented the wall in a comment, and shipped the test rather
than build the missing infrastructure.  This is honesty about
the weakness combined with a refusal to address it.
Prevention: any comment in a test body containing "might",
"should be (but we can't verify)", "cannot easily",
"approximately", "probably", or "we can verify [a different
thing]" is a code smell.  Either build the verification, or
remove the test's R-marker citation, or move the test out of
the conformance file.  Detection: grep test bodies for those
phrases; each match needs review.  This is a one-line audit
rule.

**(10) Stale-description.**  The test's description text
references a known limitation that has since been resolved;
reading the test, you'd think the issue is unresolved.
Canonical example: a test description reading *"KNOWN BUG:
TH8 does not yet auto-delete the coroutine command when the
body returns. ... This test is skipped until the C code is
fixed."* -- the bug has been fixed for over a month, but the
description never got updated.  Root cause: when a bug is
fixed, the developer updates the C code, the test runs, and
may flip from `knownBug`-skipped to passing -- but the
description text is buried inside the `runTest` block and
easily overlooked.  Prevention: when fixing a bug, grep the
test corpus for the bug's identifier and for keywords from the
known-bug description ("KNOWN BUG", "skipped until", "TODO")
and update every matching description; the signing pass after
the edit forces a re-sign, which makes the change reviewable
in a normal code-review tool.  Detection: grep all test
descriptions for "KNOWN BUG", "TODO", "FIXME", "skipped
until", "not yet", "does not yet"; each match should
correspond to a current entry in `known_bugs.md` (in which
case it's accurate) or be a stale reference (in which case it
must be updated or removed).

#### 6.17.3  Code-coverage-test failure modes (three)

These are far less common in the corpus than the
specification-test failures -- the explicitly-coverage-driven
files appear largely sound by spot-check -- but the patterns
are different and worth naming for prospective use.

**(1) Branch-not-actually-reached.**  The test's name and
comment claim to exercise a specific code path, but the body's
input doesn't actually drive execution to that path.  Branch
coverage stays unchanged when the test runs.  This is a
coverage-test analogue of failure mode (2) above (body-
unrelated-to-requirement), but with a different correctness
criterion: the test claims to provoke branch X, and the way to
verify it does is to run the suite under instrumentation
before and after adding the test, and confirm branch X's
counter increments.  Without that verification, a test named
`coverage4-Y.Z {OOM in path X}` whose body merely calls a
function and checks the return is non-empty exercises only the
happy path.  OOM coverage requires explicit fault injection.

**(2) Crash-free-is-not-correct.**  The test verifies the
implementation didn't crash on the corner case, but not that
it produced a sensible result.  A silently-corrupting bug
passes the test.  The minimal acceptable assertion beyond "no
crash" is at least one of: a structural shape check (a list of
the expected length; a string of the expected encoding), a
round-trip property `op(inverse(x)) == x`, or a dual-input
differential `op(x_safe) == op(x_corner)` where the inputs are
known to be equivalent under `op`.  Bodies of the form
`catch { ... } msg; expr {$msg eq ""}` are the canonical
crash-free-only shape and need at least one more assertion.

**(3) Input-too-weak-for-corner.**  The test's input doesn't
trigger the documented corner case because the corner-case
predicate requires more specific input than the author chose.
Example: a test claiming to exercise "list parser handles
unbalanced quotes inside braces" with an input like `{a b c}`
-- the unbalanced-quote-inside-braces path requires actual
unbalanced quotes; well-formed input never reaches that path.
Unlike spec tests where the input only needs to satisfy the
requirement's preconditions, coverage tests need inputs that
satisfy the corner case's specific shape.  Prevention: every
coverage test should have at least one comment-line specifying
the exact input shape that triggers the corner; where the
predicate is non-obvious, pin to a reproducer (e.g., a file
under `tests/fuzzing/<subType>/` from the original AFL++
campaign, or a hand-constructed minimal case with a comment
explaining each character's role).

#### 6.17.4  Cross-cutting failure modes (three)

These don't fit cleanly in either category and apply to the
suite as a whole.

**(A) Category-mismatch.**  A file's header declares it one
category, but its tests behave like the other.  TH8's
`coverage6.tcl` self-identifies as "Conformance Test File" in
its header but its 36 tests have no R-markers and exercise
escape-sequence corner cases (a code-coverage purpose).  Root
cause: category boundaries weren't enforced when files were
created; a single template gets reused; the header is
copy-paste, nobody re-reads the boilerplate.  Prevention: a
one-line CATEGORY field in the file header
(`# CATEGORY: conformance` or `# CATEGORY: coverage`), read
by the test runner; the runner reports per-category totals
and rejects R-markered tests in coverage files (and unmarked
tests in conformance files).  Detection: predominant test
shape across the file -- all tests have R-markers and
documented-property bodies (conformance) versus few/no
R-markers and corner-case bodies (coverage) -- yields the
correct classification; mixed files should be split.

**(B) Same-marker-weak-and-strong.**  Two or more tests cite
the same R-marker; one strongly verifies the requirement, the
other(s) verify it weakly.  Coverage tooling counts the
marker as covered (>= 1 test cites it).  A maintainer reading
the runner output can't tell which test is doing the actual
verification work, and a future cleanup pass could plausibly
delete the strong one and keep the weak one.  Concrete
instance: R-56935-37465 (lsort stable sort) cited by three
tests -- two weak (identical-input bodies) and one strong
(paired-element body with `lsort -index`).  Root cause: spec
tests in synthetic files are written to fill marker-coverage
gaps without checking whether the feature-organic suite
already covers the marker properly.  Prevention: before
adding a test for marker X, read every existing test that
cites X; if at least one is strong, do not add a weak one
unless it covers a distinct clause.  Detection: for each
R-marker covered by >= 2 tests, hand-review the citers;
delete weak versions unless they cover distinct clauses.

**(C) Coverage-metric-as-goal (the meta-cause).**  This is
the underlying driver of most of the §6.17.2 failure modes:
*"every R-marker has at least one test"* is a proxy for
*"every documented property is verified"*, and when the proxy
becomes the target, the underlying property gets sacrificed.
Synthetic test files that exist solely to cite R-markers
(`coverage*.tcl`) accumulate bodies optimized for "references
the marker, returns a value, asserts the value" rather than
for "verifies the property the marker pins."  This is
Goodhart's Law -- *when a measure becomes a target, it
ceases to be a good measure* -- applied at the test-suite
level.  The authoring contract for a spec test is "convince
yourself a broken implementation would fail this test," not
"convince the runner the marker is covered."  The two are
different.  A practical screening rule: any test whose body
is shorter than the description text, on a non-trivial
requirement, is suspect; in TH8's corpus the correlation was
high.

#### 6.17.5  Detection rules suitable for tooling

The detection rules above suggest a set of cheap automated
checks that could live alongside `tools/audit_patterns.tcl`
(§6.16's static checker for C source) as a `tools/audit_tests.tcl`
counterpart for the test corpus.  Each is regex-and-compare on
the extracted `(description, body, result)` triples; no test
execution required.

| Rule | Flags |
|------|-------|
| `proxy-quantifier` | DESC contains "sufficient", "at least", "beyond" -- BODY lacks corresponding inequality / range / width assertion |
| `c-api-without-testlib` | DESC contains `Th8_*` or `xNNNN` -- BODY lacks `::th8testlib::*` invocation |
| `multi-clause-single-assert` | DESC has >= 2 verbs joined by "or" / "and" / "when" -- BODY has < 2 distinct assertions |
| `conditional-without-precondition` | DESC starts/contains "when X" -- BODY lacks setup of X and lacks `-constraints` for X |
| `identical-elements-stability` | DESC contains "stable", "monotonic", "ordered" -- BODY input is identical or trivially distinct |
| `pass-anyway-disjunction` | RESULT is `{0 1}`-shape OR BODY accepts mutually exclusive outcomes |
| `constraint-equals-body` | A constraint's defining expression appears verbatim in the test body |
| `loop-variable-not-result` | DESC contains "result" / "returns" -- BODY ends in `expr {$loop_var ...}` rather than the command itself |
| `comment-admits-gap` | BODY contains "might", "cannot easily", "approximately", "should be (but...)" |
| `stale-description` | DESC contains "KNOWN BUG", "TODO", "FIXME", "not yet", "skipped until" |
| `category-mismatch` | File header says "Conformance" but >= N% of tests lack R-markers, OR header says "coverage-driven" but tests have R-markers |

These rules will produce false positives (e.g.
`pass-anyway-disjunction` on a genuinely-Boolean test) but
the false-positive rate appears manageable by spot-check on
the audit's heuristic flagger, which had a similar shape and
roughly 30/60 hit rate after hand inspection.  Hand review is
still required; the tooling's purpose is to surface
candidates, not auto-resolve.  This mirrors the codification
move §6.16 made for C-source anti-patterns: turn a one-time
audit into a permanent CI check, accept the false-positive
rate, and use `AUDIT-OK` markers for known-acceptable
exceptions.

#### 6.17.6  Lessons learned

Three observations distilled from the audit are worth
naming, in increasing order of generality.

1.  **The R-marker → test-citation map being explicit makes
    the gap measurable post-hoc.**  Many test suites have
    similar gaps between requirements and verification; few
    can quantify them without an explicit traceability
    mapping.  TH8's R-marker discipline turned what would
    have been an unobservable specification-vs-verification
    gap into a 23%-of-synthetic-tests audit finding.  The
    ability to produce that number is a feature of the
    methodology, not a defect.  Other formally-specified
    projects with similar traceability mappings (DO-178C
    avionics, ISO 26262 automotive, ISO 9899 C standard) can
    run the same audit; projects without traceability cannot
    even quantify the question.

2.  **"Test the property, not its measurable consequence"
    and "the constraint that gates a test must be
    independent of the test's body" generalize.**  These two
    rules cover the largest share of the audit's findings.
    Both are language-agnostic: they apply to JUnit + an
    English-language spec just as well as to Tcl tests + an
    R-marker'd standard.  They are the kind of rule that
    test-authoring guides have always implied but rarely
    state explicitly, and that a regression of test-quality
    over years tends to violate first.

3.  **Goodhart's Law applies to test suites and the
    specification's own structure shapes the failure modes.**
    The structure of a test corpus's metric -- "marker
    coverage" in TH8's case, "branch coverage" in code-
    centric projects, "feature checked" in QA-led projects --
    determines which failure modes are most likely.  TH8's
    marker-coverage metric produced exactly the failures
    enumerated above; a branch-coverage-driven corpus would
    produce the §6.17.3 failures predominantly; a feature-
    checked corpus would produce something different again.
    The lesson is not "metric X is bad."  The lesson is
    "every metric for test-suite strength has a predictable
    corruption vector, and a periodic audit against the
    underlying property the metric is a proxy for is the
    only way to detect drift."  TH8's marker-coverage audit
    is straightforwardly translatable to "branch-coverage
    audit" or "feature-coverage audit" in those other
    settings.  The detection rules vary; the discipline does
    not.

Practical takeaway for embedders and other formally-
specified projects: write the audit script before you reach
"100% coverage."  Run it the moment you cross 90%.  The cost
of catching a §6.17.2 failure mode at month six is one
edit; at year three it is potentially a published
specification with weak tests pinned to its requirements.

### 6.18  Methodology: Tests as Data

The §6.17 audit was tractable because TH8's test corpus has
a structural property that other testing methodologies do not
share: each test is *data*, not code.  This subsection makes
that property explicit, examines what it enables, compares
against xUnit / pytest / JUnit, and connects to the
relational-data theory that justifies why test-as-data is so
mechanically friendly.

#### 6.18.1  The runTest record

A TH8 test is a single Tcl call to the `runTest` wrapper.  Its
arguments are a dictionary in everything but the surface
syntax: each `-name`-style keyword is a field, each
following block is the field's value.  The full record:

```tcl
runTest {test coverage3-8.1 {
  R-19715-59368: Integer arithmetic that would overflow ...
} -constraints {
  bigint_toggle
} -setup {
  ::th8testlib::bigint disable
} -body {
  catch {expr {0x7FFFFFFFFFFFFFFF * 2}}
} -cleanup {
  catch {::th8testlib::bigint enable}
} -result {1}}
```

This is a typed record with the fields:

| Field        | Type | Required | Purpose |
|--------------|------|---------:|---------|
| `name`       | identifier | yes | unique test identifier; convention `<area>-<section>.<index>` |
| `description`| free-text block | yes | first line cites zero or more `R-NNNNN-NNNNN:` markers |
| `constraints`| name list | no | every constraint must be true at runtime; otherwise SKIPPED |
| `setup`      | script | no | runs before body; failure marks test FAILED |
| `body`       | script | yes | the test's verification logic |
| `cleanup`    | script | no | runs unconditionally after body, even on failure |
| `result`     | string | yes | expected body return value |
| `match`      | enum {`exact`, `glob`, `regexp`} | no | match mode for `result`; default `exact` |
| `returnCodes`| code list | no | acceptable Tcl return codes; default `{0}` |

Every test in the 91-file corpus is a record of this shape.
There are no per-test methods, no instance fields, no class
hierarchies, no annotations bound to method declarations.
The test is the record.

#### 6.18.2  The corpus as a relation

Once individual tests are typed records, the test corpus is a
relation in Codd's sense: a set of tuples drawn from a
fixed schema.  Audits, mass rewrites, and corpus-level
queries become operations on the relation.  The §6.17 audit's
60-line Python extractor is a single SELECT over the corpus:
project the (`description`, `body`, `result`) columns;
filter where `body` lacks language matching the requirement
text's risk-keywords; emit one row per candidate finding.
Other corpus operations have similar shapes:

- **Coverage check:** project (`description.R-marker`); join
  against the standard's R-marker set; compute the orphan and
  uncovered residuals.  This is what `tools/mkreq.tcl
  --check-tests` does; it is fundamentally a relational join.
- **Constraint usage:** project (`constraints`); aggregate
  by constraint name; compare against the registered
  constraint set in `lib/Standard1.0/test.tcl`.  Catches
  typos in constraint citations and constraints that no
  longer have a defining `testConstraint` call.
- **Mass rewrite:** map a transform function over each
  record; emit the rewritten corpus.  This is how the
  2026-04-29 R-marker rename pass worked: `OLD -> NEW` on
  the `description` field, no other field touched.
- **Test generation:** construct records from a higher-level
  description (e.g., "for every R-marker in §29.7, emit a
  test that exercises the marker's first verb").  The
  output is a corpus in the same shape; nothing
  language-specific to the implementation is needed.

A test corpus that is also a relation is amenable to the same
toolset that operates on databases.  CSV / JSON / YAML
reporters, SQL extractors, R / Python / Julia analysis
notebooks, all work directly on the extracted records.

#### 6.18.3  Comparison with xUnit / pytest / JUnit

Consider the same test expressed as JUnit 5 with Java:

```java
@Test
@EnabledIf("bigintTogglePresent")
@DisplayName("R-19715-59368: Integer overflow when checking enabled")
void coverage3_8_1() {
  th8testlib.bigint("disable");
  try {
    int rc = th8.tryCatch(() -> th8.expr("0x7FFFFFFFFFFFFFFF * 2"));
    assertEquals(1, rc);
  } finally {
    th8testlib.bigint("enable");
  }
}

static boolean bigintTogglePresent() {
  return th8.commands().contains("::th8testlib::bigint");
}
```

The fields are still present: name (`coverage3_8_1`),
description (`@DisplayName` annotation), constraint
(`@EnabledIf("bigintTogglePresent")`), setup (the first line
of the method body), body (the assertEquals call), cleanup
(the `finally` block), expected result (the literal `1` in
assertEquals), match mode (implicit in assertEquals's
equality check).  But each field is *encoded into a different
shape*: annotations, method bodies, try/finally constructs,
helper methods.  Extracting (`name`, `description`,
`constraint`, `setup`, `body`, `cleanup`, `result`) from a
JUnit corpus requires:

1.  Parsing Java source (or compiled bytecode + annotation
    metadata).
2.  Walking method bodies as ASTs to identify which
    statements are setup vs. body vs. cleanup.  No
    syntactic marker distinguishes them; the convention is
    "first line(s) before the assertion."
3.  Recognizing assertion calls and extracting their
    arguments as the expected result.
4.  Resolving annotation references (`bigintTogglePresent`)
    back to their source method to identify the constraint
    predicate.

Each of these is solvable, but each adds infrastructure.  The
xUnit ecosystem has tools for some of them (`@DisplayName`
parsing, `@EnabledIf` lookup, `assertEquals` argument
extraction via test-runner libraries) but they are
*additions* to the core framework.  The audit described in
§6.17 would require parsing tooling that the framework
doesn't ship; the same audit on a TH8 corpus is 60 lines of
Python.

pytest is closer to test-as-data than JUnit, because pytest
fixtures encourage a more declarative style and `@pytest.mark`
gives some annotation-style metadata.  But pytest tests are
still Python functions whose body is procedural code; the
description is still a docstring; the setup is still
`@fixture(autouse=True)` or in-method `setUp` code; the
cleanup is still `try/finally` or fixture teardown.  The
rough order-of-magnitude cost of corpus-level operations is
the same.

The advantages of test-as-data over test-as-code, listed
explicitly:

1.  **Audit cost.**  60 lines of extractor for a corpus
    audit, not parser-walking infrastructure.  The §6.17
    audit was authored, debugged, and run in approximately a
    single working day.

2.  **Specification traceability is a property of the test,
    not of external tooling.**  R-marker citations live in
    the `description` field as literal text.  The mapping
    requirements -> tests is a column in the corpus
    relation.  No annotations, no IDE plugins, no separate
    matrices.

3.  **Mass rewrites are sed / awk / a 30-line Tcl script.**
    The 2026-04-29 R-marker rename pass changed identifiers
    across 250 test files in seconds; an equivalent JUnit
    rewrite would invoke an AST-based refactoring tool
    (IntelliJ's Structural Search and Replace, or a
    Spoon-based transformer) with the matching pattern
    expressed in a tool-specific DSL.

4.  **Cleanup is mandatory.**  The runner runs the
    `cleanup` field even if the body fails or errors.
    JUnit's `@AfterEach` provides this guarantee for class-
    scoped fixtures, but per-test cleanup needs a `try
    /finally` that the author may forget to write.

5.  **Resource mutation is detected automatically.**  The
    `runTest` wrapper snapshots procedure / variable /
    command / namespace / channel / package / function /
    expansion / breakpoint sets before and after each test
    and reports any non-empty diff as MUTATED.  No xUnit
    framework offers this out of the box; each leak class
    needs a per-test cleanup discipline that the author
    writes by hand.  The 2026-08-07 audit reports
    `MUTATED: 0` across 4,451 tests -- a property xUnit
    suites generally cannot claim without per-test cleanup
    boilerplate.

6.  **Constraints are first-class, named, registry-backed
    predicates.**  `crypto_enabled`, `bigint_toggle`, and
    `file_tempname` are test-suite-global identifiers
    defined once in `lib/Standard1.0/test.tcl` and reusable
    by every test in every file.  JUnit's `@EnabledIf`
    annotations can reference helper methods, but
    cross-test reuse requires explicit imports and
    visibility management.  pytest's `@pytest.mark` is
    closer but still per-fixture and per-conftest.py.

7.  **Result match modes are first-class.**  `-match
    regexp` for the floating-point output that varies by
    libm; `-match glob` for error-message families; default
    `exact` for canonical results.  The `result` field's
    interpretation is part of the record.  In xUnit this
    requires a `Matcher` argument to a specific assertion
    helper; the choice is per-call, not per-test.

8.  **Cross-engine portability is a property of the form.**
    A test that uses only commands present in TH8, Eagle,
    and upstream Tcl runs identically in all three engines.
    The runner is a Tcl script; no language-specific
    discovery, no compile step, no module path.  xUnit
    tests are wedded to their runtime: a JUnit test does
    not run under Python without rewriting.

9.  **The test-as-data form survives mechanical evolution.**
    When a constraint name changes, when the runner adds a
    new field (e.g., the resource-mutation tracking added
    in 2026-04), when a new audit rule joins the toolchain
    -- none of these require touching test bodies.  In an
    xUnit corpus, an analogous evolution typically requires
    a code-mod pass over every test.

#### 6.18.4  Theoretical lens

The structural advantage of test-as-data has a long
intellectual heritage worth naming.  Three lenses are
useful, in increasing order of formality.

**The dictionary lens.**  A test record is literally a
typed dictionary with the schema in §6.18.1.  Tcl's `runTest`
surface is keyword-argument style for ergonomics, but the
record is round-trippable to a Tcl dict
(`dict create name coverage3-8.1 description ... constraints
{bigint_toggle} setup ...`), a JSON object, a Python
dataclass, or a CSV row, with no semantic loss.  This is the
property that makes corpus-level tooling cheap: any tool that
operates on dictionaries can operate on the corpus.

**The relational lens.**  Codd's relational model defines
relations as sets of tuples drawn from a typed schema.  The
TH8 test corpus is a relation in this exact sense: rows are
tests, columns are fields, the schema is fixed.  Codd's
algebra (selection, projection, join, union, intersection,
difference) directly maps to corpus operations:

- *Selection (filter):* find every test with a
  `bigint_toggle` constraint -> `WHERE 'bigint_toggle' IN
  constraints`.
- *Projection:* extract (R-marker, body, result) for an
  audit -> `SELECT description, body, result`.
- *Join:* match tests against the standard's R-markers ->
  `tests JOIN markers ON tests.description LIKE
  '%' || markers.id || '%'`.
- *Aggregation:* count tests per constraint -> `SELECT
  constraint, COUNT(*) FROM tests, UNNEST(constraints) AS
  constraint GROUP BY constraint`.

Audit, coverage, and code-modification tools that look like
ad-hoc scripts in a test-as-code corpus turn into 5-line
SELECT statements over a test-as-data corpus.  The audit's
insight in §6.17 is fundamentally that "23% of synthetic
spec tests fail the §6.17.2 rubric" is a `SELECT COUNT(*) ...
WHERE ...` query against a relation that already exists.

**The homoiconicity lens.**  Lisp and Tcl share a property
absent from Java / Python / C++: the language's source
representation IS its data structure.  A Tcl script is a
list of words separated by whitespace; words are strings or
nested lists.  The `runTest` record is a Tcl list whose
elements happen to be name / option-keyword / option-value
words.  Reading the corpus does not require a parser
separate from the language's own; the Tcl parser already
produces the data structure the audit needs.

This is the same property that makes Lisp macros tractable:
code is data.  In the testing context, it means the test is
a value the runner consumes, not a function the runner
calls.  The mechanical-analysis affordances follow
immediately.

(Java's bytecode, Python's `ast` module, C++'s libtooling --
each provides AST access to source code, but at the cost of
parsing.  Tcl's homoiconicity makes the AST equal to the
source text after `[list]`-shaping.  The cost gap is several
orders of magnitude.)

#### 6.18.5  Tradeoffs

Test-as-data is not a free advantage.  The honest list of
what xUnit / JUnit / pytest do better:

1.  **Built-in mocking and stubbing.**  Mockito (JUnit) and
    `unittest.mock` (pytest) provide call-recording,
    argument-capturing, and stubbing semantics that Tcl test
    suites build by hand using `proc name {} { ... }`-style
    overrides.  A pure-Tcl mocking library exists in some
    forks but is not part of TH8's `runTest` framework.

2.  **IDE integration.**  IntelliJ / VS Code / Eclipse offer
    "click the gutter to run this test" widgets, in-test
    debugger breakpoints, per-test result panels, and
    failure-jumping that Tcl test runners do not provide
    out of the box.  TH8's runner is a CLI; no GUI surface.

3.  **Parallel execution.**  Tcl's per-interp single-
    threaded model means tests run sequentially within a
    single shell.  JUnit's `@Execution(CONCURRENT)` and
    pytest's `pytest-xdist` can fan out across cores or
    machines.  TH8's runner can be parallelised at the
    file level (each file in a separate `th8sh` process),
    but the `runTest` framework itself is sequential.

4.  **Reporting ecosystem.**  HTML reports, Codecov badges,
    flake-tracking dashboards, JUnit-XML format consumed by
    CI systems are abundant in xUnit.  TH8's runner emits
    plain text; CI integration requires post-processing.

5.  **Constraint registry is global state.**  A constraint
    redefined mid-suite changes behavior for all subsequent
    tests in the same shell process.  JUnit's per-test
    annotation is more contained.  TH8's convention is to
    define constraints once in
    `lib/Standard1.0/test.tcl::setupCommonConstraints`;
    nothing in the framework prevents an out-of-band
    redefinition.

The absence of a plugin ecosystem is also a virtue: there
is no version-skew between the runner and a hundred
third-party plugins; no plugin compatibility matrix to
maintain.  The trade is real but pointed in the direction
of simplicity.

#### 6.18.6  When to choose test-as-data

Not every project benefits from this discipline.  The
preconditions for test-as-data to pay off:

1.  **The test corpus is large** -- thousands of tests,
    not dozens.  At small scale, the audit overhead of
    test-as-code is rounding error.
2.  **Tests must be machine-traversed** -- for coverage,
    audit, mass-rewrite, code-mod, automated generation.
    A test corpus that is only ever read and authored by
    humans does not benefit.
3.  **The system under test is itself amenable to
    declarative testing** -- given inputs, observe
    outputs.  Property-based testing over stateful systems
    can express in test-as-data form, but the data shape
    grows; non-trivial domain-specific logic is awkward
    inside a `body` field.

Languages with formal specifications, language interpreters
with conformance suites, parsing tools, query engines, and
compilers all match this profile.  Web applications,
event-driven systems, stateful workflows are less natural
fits -- the body field grows and the test-as-data
advantages dilute.

TH8 fits the profile by construction: it is a language
interpreter with a published normative specification.  The
test corpus is a verification matrix between the spec and
the implementation, not a behavior-driven scenario suite.
This is why the test-as-data form is a structural fit,
not a stylistic preference.

### 6.19  Case Study: The Setup-Cleanup-Constraint Pattern

The §6.18 methodology is abstract; this case study grounds
it.  The example is the bigint-toggle test rewrite on
2026-05-04, which closed the SKIPPED-GAP failure mode in
`coverage3-8.1` (R-19715-59368, integer-overflow error when
overflow checking is enabled).

#### 6.19.1  The starting point

Before the rewrite, the test was:

```tcl
runTest {test coverage3-8.1 {
  R-19715-59368: Integer arithmetic that would overflow ...
                 shall produce an error when overflow
                 checking is enabled
} -constraints {
    overflow_check
} -body {
  catch {expr {0x7FFFFFFFFFFFFFFF * 2}}
} -result {1}}
```

with the `overflow_check` constraint defined as:

```tcl
testConstraint overflow_check [expr {
  [catch {expr {0x7FFFFFFFFFFFFFFF * 2}}] == 1
}]
```

This is the `CONSTRAINT-EQUALS-BODY` failure mode (§6.17.2,
pattern 7) in its purest form: the constraint expression is
the test body.  Under TH8's default-bigint configuration,
the multiplication succeeds (returns an arbitrary-precision
result), the constraint evaluates to false, and the test is
SKIPPED.  Under any hypothetical non-bigint configuration
where the constraint would be true, the body is the same
expression and would necessarily pass.  The test cannot
fail by construction; in practice it can only ever be
skipped or pass.  The R-marker is recorded as covered
because at least one test cites it.  The implementation is
not actually verified.

#### 6.19.2  The fix

The rewrite has three parts.  First, a new C-level command
in the test surface:

```c
static int
th8test_bigint_cmd(...)
{
    /* th8testlib::bigint enable|disable|query */
    if (argl[1] == 6 && memcmp(argv[1], "enable", 6) == 0) {
        return Th8_EnableBigint(interp, 1);
    }
    if (argl[1] == 7 && memcmp(argv[1], "disable", 7) == 0) {
        return Th8_EnableBigint(interp, 0);
    }
    if (argl[1] == 5 && memcmp(argv[1], "query", 5) == 0) {
        Th8_SetResultStatic(interp,
            Th8_IsBigintEnabled(interp) ? "1" : "0", TH8_NOLEN);
        return TH8_OK;
    }
    /* unknown subcommand error ... */
}
```

`Th8_EnableBigint` and `Th8_IsBigintEnabled` are existing
public C APIs (originally added for the embedder use case
of disabling bigint at startup); the new test command is a
thin wrapper exposing them at script level for tests only.

Second, a new constraint that probes the *capability*, not
the test's body:

```tcl
testConstraint bigint_toggle [expr {
  [llength [info commands ::th8testlib::bigint]] > 0
}]
```

Third, the rewritten test that uses setup / cleanup to
engage the precondition the requirement specifies:

```tcl
runTest {test coverage3-8.1 {
  R-19715-59368: Integer arithmetic that would overflow ...
                 shall produce an error when overflow
                 checking is enabled
} -constraints {
    bigint_toggle
} -setup {
  ::th8testlib::bigint disable
} -body {
  catch {expr {0x7FFFFFFFFFFFFFFF * 2}}
} -cleanup {
  catch {::th8testlib::bigint enable}
} -result {1}}
```

The body is unchanged.  The body's pass-criterion (catch
returns 1) is now genuinely meaningful: it asserts the
implementation raises on overflow, which is exactly what
R-19715-59368 says.  The test no longer can-only-skip-or-
pass; it can fail if a future change to the multiply path
silently swallows overflow even with bigint disabled.

#### 6.19.3  The pattern, generalized

This is the **Setup-Cleanup-Constraint Pattern** for
state-engaging tests.  The shape:

```tcl
runTest {test <name> {
  R-XXXXX-XXXXX: <requirement specifying behavior under condition X>
} -constraints {
    <toggle_capability_present>
} -setup {
  <engage condition X>
} -body {
  <invoke operation>
  <assert documented behavior under X>
} -cleanup {
  catch {<restore default state>}
} -result {<expected behavior>}}
```

The five structural elements:

1.  **Constraint gates on capability presence**
    (`bigint_toggle`), not on the test's body or its
    expected outcome.  Independence of constraint and body
    rules out the §6.17.2 pattern 7 anti-pattern.
2.  **Setup engages the condition the requirement names.**
    The requirement is "shall do X when Y"; setup
    establishes Y.  The body then verifies X.
3.  **Body verifies the documented behavior under Y**, with
    an assertion that depends on the implementation's
    actual response, not on a proxy observable.
4.  **Cleanup restores the default state**, wrapped in
    `catch` so a body failure does not leak the engaged
    condition into subsequent tests.  The runner runs
    cleanup unconditionally.
5.  **Result match is exact** for the canonical observed
    outcome.

This pattern fits every test that asserts conditional
behavior: signed-only mode toggles
(`::th8testlib::signed_only`), policy-callback installs
(`::th8testlib::preeval install`), fault injection
(`::th8testlib::fault`), and any future runtime-toggleable
mode.  It is the natural composition of test-as-data with
the precondition-then-assert structure.

#### 6.19.4  Why test-as-data makes the pattern auditable

The pattern's value is not just that it produces a stronger
test.  It is that the test now **carries its precondition
in the record**: the `setup` field literally contains the
state-engaging command, the `cleanup` field literally
contains the restore command, the `constraints` field
literally names the capability gate.  An audit script can
query the corpus for this shape:

```python
for test in corpus:
    if 'bigint' in test.body and not test.setup:
        flag(test, 'engages bigint behavior without explicit setup')
    if test.setup and not test.cleanup:
        flag(test, 'setup engages state without cleanup restore')
    if expression_in(test.constraints) == test.body:
        flag(test, 'constraint-equals-body anti-pattern')
```

Each rule is one line of pattern matching against the
corpus relation.  The test-as-code analogue requires AST
walking, control-flow analysis, and matching against
language-specific idioms (Java's `try/finally`, Python's
context managers, C++'s RAII destructors) -- each of
which expresses the same semantic with different syntactic
shape.

#### 6.19.5  Empirical outcome

Before the rewrite: `coverage3-8.1` SKIPPED.  R-19715-59368
recorded as covered (the test cites it); not actually
verified by any test in the corpus.

After the rewrite: `coverage3-8.1` runs in the
`bigint_toggle`-available build and PASSES.  The test
exercises the actual overflow-error path via setup-engaged
non-bigint mode; if a future change broke that path, the
test would fail.  The R-marker is genuinely covered.

The rewrite also enabled a separate observation that
strengthens the methodology: because the
setup-cleanup-constraint pattern is so structurally
distinct, an audit query can identify other tests that
*should* use the pattern but don't.  Twelve tests in the
TH8 corpus currently engage build-conditional or runtime-
toggleable state inside the body without an explicit
setup-cleanup pair; the audit's next pass will surface
them.  This is the meta-value of test-as-data: patterns
that are ergonomic in the form become enforceable as
audit rules.

#### 6.19.6  Generalizable lesson

The setup-cleanup-constraint pattern is not Tcl-specific.
xUnit / JUnit / pytest can all express the same idea.  The
TH8 lesson is that test-as-data **makes the pattern
auditable** -- an audit rule that "every test engaging
state X must have a setup that engages X and a cleanup
that restores it" is a five-line pattern match against the
corpus, not an AST analysis pass.

For embedders writing similar test suites: the
combination of *runtime-toggleable test-only state*
(via testlib helpers) plus *capability-presence constraints*
plus *the runTest record shape* gives full coverage of
conditional requirements without the CONSTRAINT-EQUALS-BODY
trap.  This is the recommended pattern for any
specification that contains "X shall happen when Y is true"
clauses, which is most of them.

### 6.20  Case Study: Underspecified Phrasing and the [binary] BigInt Arc

A recurring failure mode in agent-driven development is the
**minimal interpretation of an ambiguous request**: when an
instruction has both a cheap reading and an expensive reading,
the agent picks the cheap one, ships it, declares it done, and
the human only later discovers that the feature it implemented
is not the feature it was supposed to implement.  The
`[binary]` ensemble's BigInt support is a clean example
because the arc fits in one work session and both endpoints
are concrete, script-visible behaviors.

**The request.**  The human asked, in two messages over
roughly an hour: "please be sure that BigInt is supported as
well... per the new Tcl 8.6 TIP(s), I believe," then later
"did you add KIND_BIGINT and make the Bigint stuff 'first
class' within the `[binary]` ensemble subsystem? That is the
goal.  Of course, it must be compile gated behind
`TH8_ENABLE_BIGINT` as well as runtime gated, per
`Th8_IsBigintEnabled(interp)`."

**The cheap reading.**  The agent read "first class" as
*accept bigint inputs anywhere an integer is accepted today*,
implemented it that way --- a new `KIND_BIGINT` dispatch tag,
a `th8BinaryParseBigint` helper that called the native bigint
arithmetic engine with a bitwise-AND against a mask string
`"18446744073709551615"` (= 2^64 - 1), and the
compile/runtime gates as requested.  Tests passed.  The fixed-
width integer specifiers (`c`/`s`/`i`/`w`/etc.) now accepted
arbitrary-precision bigint inputs and silently *truncated*
them modulo the field width.

**The expensive reading.**  Days later, after the manpage was
written and the standard's R-markers were locked in, the human
re-read the implementation and replied: "I am confused.  The
`[binary]` ensemble should not simply truncate bigint values
to 32-bit / 64-bit... it should be capable of importing /
exporting the actual bits for the bigint values."

The cheap-vs-expensive distinction is now stark.  The cheap
reading mirrors Tcl 8.6's actual behavior (Tcl 8.6 *does*
truncate bigints to the integer specifier width).  The
expensive reading goes beyond Tcl 8.6: it requires an
arbitrary-width pack/unpack with full bit fidelity --- a
feature Tcl 8.6 does not have at all.  The agent's reading
was *consistent with the reference* but inconsistent with
what the human meant by "first class."

**The clarification round.**  Rather than guess at the design,
the agent enumerated the orthogonal choices and asked the
human via a four-question multi-choice survey:
specifier-letter, count semantics, `*` semantics, and
encoding.  The human answered: dedicated letters `j` (LE) and
`J` (BE), count = byte width, `j*` = minimum-bytes-needed,
encoding = two's complement.  A side-note in a later message
added: "On binary scan, `jN` needs an explicit signedness
rule.  Always signed (matches `c`/`s`/`i`/`w`'s default ---
sign-extend on read)."  Each of these design points then
became an R-marker (R-64289-18387, R-22686-46265,
R-54898-61945, R-07235-20862, R-05827-41628) in the language
standard, anchored to test cases (`binary-j-1.x`..`-4.x`),
documented in the manpage's BINARY DATA subsection.

**What was actually new.**  The `j`/`J` specifiers are not in
Tcl 8.6.  They give TH8 a primitive that Tcl 8.6 does not:
round-trippable bigint-to-bytes conversion for *any* width,
not just the fixed 1/2/4/8 the integer specifiers offer.  An
asymmetric-cryptography signature blob, a 256-bit hash, a
multi-precision elliptic-curve scalar --- these now fit in
the language without resort to hex-string manipulation.  The
expensive reading was, in hindsight, the right reading; the
cheap reading was a strictly-smaller subset that just
happened to *look* complete because Tcl 8.6 is the same
strictly-smaller subset.

**Two lessons.**  First: when an instruction names a feature
("BigInt support") and a quality bar ("first-class") but not
the *behavior*, the agent should treat the gap as a design
question and ask before implementing, not after.  Second:
matching a reference implementation is a useful sanity check
but it is not a substitute for matching the human's intent.
The agent's BigInt-as-mod-2^N implementation was *behaviorally
identical to Tcl 8.6* and *semantically wrong*.  Conformance
with prior art is necessary but insufficient when the project's
goal is to *exceed* the prior art.

For agentic-AI methodology: the cure for the
minimal-interpretation failure mode is not "ask for
clarification on everything" (that wastes the agent's main
value, which is doing work without supervision) but to
**explicitly enumerate the cheapest plausible reading
alongside an expensive one when the instruction is genuinely
ambiguous**, and let the human pick.  The four-question
survey at the start of the `j`/`J` design pass took roughly
two minutes of human time and saved a complete second
re-implementation pass --- the agent would have built another
truncating bigint variant otherwise.

### 6.21  Case Study: The Limits of Memory Tooling

A layout-sensitive heap-corruption bug --- an intermittent wild
eight-byte write of high-entropy data onto a single pointer field
inside the live interpreter struct --- produced the most
instructive negative result in the project: a demonstration of
where the standard memory-safety toolchain simply cannot see.

Three tools were expected to catch it, and none could, for the
same structural reason.  AddressSanitizer places red zones
*around* allocations and poisons *freed* memory; it faults on
out-of-bounds and use-after-free accesses.  But the corrupted
field sits at a valid, in-bounds offset of a live allocation that
is never freed --- there is no red zone at that address and the
memory is not poisoned, so a write landing there is, to ASan, a
perfectly legal store.  mimalloc's secure mode (guard pages,
encoded free lists, double-free detection) and the platform's
heap-consistency check (`malloc_zone_check`) share the blind spot:
they validate allocator *metadata* and detect *boundary*
violations, not arbitrary in-bounds writes to user data.

Worse, the project's own performance choice had silently disabled
the instrumentation entirely.  TH8 routes allocation through
mimalloc by calling `mi_malloc`/`mi_free` directly; ASan and
`libgmalloc` intercept the *system* allocator, which mimalloc
bypasses.  An early "ASan-clean" result was therefore meaningless
--- ASan had never instrumented a single interpreter allocation.
Only a rebuild with mimalloc disabled put the heap under the
sanitizer's view, and even then the in-bounds write remained
invisible for the reasons above.

Roughly four hundred reproduction attempts across build
configurations, allocator settings, and a hardware-watchpoint
harness failed to reproduce the fault on demand; the corruption is
gated on per-run heap-layout jitter from the network and TLS path.
A five-agent static audit (Section 6.23) then ruled out every
*structured* write path by construction --- use-after-free (which
lands in freed blocks, not the live struct), callback-context
type-confusion (no context ever resolves to the interpreter),
mis-targeted field stores, and even ABI/layout divergence between
translation units (the field precedes every conditionally-compiled
member, so its offset cannot shift).  What remains is a
wild-pointer write to a computed in-bounds address, which no
source-scoped audit can localize and no red-zone tool can trap.

The episode motivated a hardening now on TH8's roadmap: a
*read-only-after-init* region.  Many interpreter fields ---
dispatch tables, the system-variable hash, key material, the
policy callback --- are written once at setup and never again.
Grouping them onto their own page and marking it read-only
(`mprotect` / `VirtualProtect`) for the interpreter's lifetime
converts any wild write to them from a silent, layout-dependent
Heisenbug into an immediate fault *at the writing instruction* ---
the one place a destination guard succeeds where every
allocation-oriented tool fails.  Unlike a debug-only watchpoint,
this is a shippable integrity feature in the spirit of ELF
`RELRO`.

A smaller tooling lesson rode along.  Building the
maximal-detection allocator was blocked by an assertion in the
vendored allocator's own validator that fired on every trivial
program.  It was not a real corruption: an auto-annotated
invariant compared the wrong two quantities --- a memory segment's
total slice count against its nominal size rather than its
deliberately-lower usable-entry count.  Machine-generated
invariants are code too; they must be validated against the actual
semantics, not trusted because a tool emitted them.

The lessons are general:

1.  **Know what your sanitizer instruments.**  A custom allocator
    that bypasses the system heap silently blinds every tool that
    hooks the system heap.  "ASan-clean" means nothing until you
    have confirmed the sanitizer sees the allocations you care
    about.

2.  **Red zones do not cover in-bounds corruption.**  Sanitizers,
    guard pages, and zone checks catch boundary and lifetime
    violations.  A wild write to a valid, in-bounds address of a
    live object is beyond all of them; only a destination guard
    --- a watchpoint or read-only memory --- can trap it.

3.  **When reproduction fails, prove absence instead.**  Weeks of
    reproduction attempts yielded less than a structured static
    audit that eliminated whole mechanism classes by construction.
    For a layout-sensitive Heisenbug, exhaustive elimination is a
    better use of effort than a reproduction lottery.

### 6.22  Case Study: Two Dispatch Paths, One Bug Class

TH8's non-recursive evaluator (Section 3.2) trades C-stack
recursion for an explicit chain of heap-allocated continuation
callbacks.  That trade buys unbounded evaluation depth and clean
coroutine support, but it has a cost the paper should state
plainly: it splits command dispatch into *two* code paths --- a
fast synchronous word-splitter for commands with no command
substitution, and an NRE continuation path for commands containing
`[...]` --- and two paths that implement the same logic will
drift.

Two bugs found in the same audit were the same drift, twice.  The
first was a use-after-free.  When a command resolves to no known
name, both paths build an `unknown`-prefixed argument vector whose
element pointers borrow from the original vector, then dispatch the
`unknown` handler.  The synchronous path defers freeing that
wrapper vector to a cleanup callback, with an explicit comment that
it must *not* be freed synchronously because a proc handler binds
its parameters in a *later* continuation that still reads the
vector.  The NRE path --- written separately --- freed it
immediately after the handler call returned.  For a proc handler,
which defers its parameter binding to a pushed callback, that is a
textbook use-after-free; AddressSanitizer confirmed it
deterministically, reading freed memory in the parameter-binding
continuation.  The fix made the NRE path defer the free exactly as
the synchronous path does.

The second bug was a missing security check.  The synchronous path
rejects a *tainted command name* --- selecting which command to run
from untrusted data is code selection and must never occur.  The
NRE path omitted the check, so a command name assembled through
`[...]` substitution from tainted data could be dispatched ungated.
This is not a memory bug but a security-policy hole, and it is the
identical failure mode: two paths meant to enforce the same
invariant, one of which forgot.

The repair addressed the class, not just the instances.  The taint
check was factored into a single helper that both paths call, so
the invariant now has exactly one implementation; the two call
sites cannot diverge because there is nothing to diverge.  (As a
bonus, discussed in Section 4.13, the single shared decision is
also cleaner to cover than two duplicated compound conditions.)
The use-after-free was fixed by making the NRE path mirror the
synchronous one, and a regression test now exercises the
`unknown`-via-substitution path that no existing test reached.

The lessons are general:

1.  **Duplicated control flow is a standing invariant hazard.**
    Any time the same logical operation is implemented in two
    places --- a fast path and a general path, a synchronous and an
    asynchronous variant --- the invariants they share will
    eventually be enforced in only one.  The defect is not in
    either path; it is in the duplication.

2.  **Factor the invariant, not just the code.**  Sharing a helper
    for the *decision* --- here, "is this command name tainted?" ---
    is stronger than sharing a helper for the mechanics, because it
    makes divergence structurally impossible rather than merely
    unlikely.

3.  **The NRE design's benefits are real but its costs are
    concrete.**  Explicit continuation state reintroduces the
    manual-lifetime hazards --- use-after-free, premature free,
    leaks on abnormal exit --- that C-stack recursion handles
    automatically.  A paper advocating non-recursive evaluation
    owes its readers this caveat.

### 6.23  Case Study: Multi-Agent Adversarial Auditing

The methodology of Section 6.1 describes sustained single-session
collaboration with one LLM.  Hunting the layout-sensitive
corruption of Section 6.21 pushed the collaboration into a
different shape worth reporting on its own: a *fan-out* of
independent agents, each auditing a partition of the codebase
against a shared, precisely-specified bug signature, whose
collective result is a bounded proof of absence.

The technique was used twice.  First, to test the hypothesis that
the corruption was a stale-pointer *write* through the
interpreter's growable-string append path, five agents partitioned
the roughly three hundred call sites of the append primitives
across the source tree and each verified, function by function,
that no caller retained a pointer into an accumulator across a
reallocating append.  The uniform result --- every accumulator is a
distinct object from every append source --- was itself a finding:
it eliminated the hypothesis and redirected the hunt.  That
redirect surfaced the use-after-free of Section 6.22.

Second, to localize the wild *write*, five agents partitioned the
plausible mechanisms rather than the files: one took the
continuation-callback lifetimes, one the cryptographic write paths,
one the platform-callback contexts, one the interpreter struct's
own writers and cross-translation-unit layout, and one the event
and coroutine state machine.  Each was given the exact target ---
an eight-byte high-entropy write to one named field at a known
offset --- and instructed to report only a concrete, named store or
to certify its scope clean.  All five certified clean, and their
*combined* certifications ruled out use-after-free, type-confusion,
mis-targeted stores, and ABI divergence by construction.  The bug
was not found, but the *space* it could hide in was reduced to a
single category, which is precisely the information needed to
choose the next tool --- the destination guard of Section 6.21 ---
over continued source inspection.

Two properties made this productive.  The agents were *adversarial
and specific*: each was told to name a concrete offending store or
declare absence, not to "look for bugs," which turns a vague search
into a checkable claim.  And they were *partitioned to be
independent*: scope disjointness means their absence-proofs compose
--- clean-across-all-scopes is a real statement about the whole ---
in a way a single agent sweeping everything cannot cheaply provide,
because it cannot hold the whole system in view at once.

The lessons are general:

1.  **Absence is a legitimate deliverable.**  An audit that finds
    nothing is not wasted if its scope was precise: "no
    stale-pointer write exists among these three hundred call
    sites" eliminated a hypothesis that had consumed days of
    reproduction effort.

2.  **Partition to compose.**  Independent agents over disjoint
    scopes yield certifications that combine into a statement about
    the whole.  The partition is the point; it is what turns
    parallel effort into a coverage argument.

3.  **Specify the signature, not the goal.**  "Find the store of
    eight high-entropy bytes to this field at this offset, or
    certify your files cannot produce it" is checkable; "find the
    memory bug" is not.  The precision is what makes a fan-out of
    agents trustworthy rather than merely fast.

### 6.24  Case Study: Two False Coroutine Bugs and Reproducer Discipline

Section 6.17 catalogs the ways synthetic *tests* fail.  This case
study is about the ways a synthetic *reproducer* fails --- the
script an agent writes to demonstrate a suspected defect --- and it
is worth separating because a bad reproducer is more dangerous than
a bad test.  A bad test fails loudly and gets discarded; a bad
reproducer *succeeds* at failing, and its failure is then read as
evidence of a product bug that does not exist.  Acting on that
evidence means "fixing" correct code.

The pattern appeared twice, consecutively, in the same delicate
subsystem: the non-recursive evaluator's coroutine and suspension
machinery (Section 3.2).  The first instance, provisionally logged
as a yield-through-event-pump bug, reproduced a coroutine that
appeared to lose its continuation after yielding inside an event
callback.  The reproducer queued *two* asynchronous worker-thread
events and resumed the coroutine twice; the events raced, and on
the ordering where one event completed the wait before the other
delivered its yield, the coroutine finished early and its command
was deleted.  The "bug" was the race in the reproducer.  A
pre-existing deterministic single-event test already proved the
real behavior correct.  The finding was retracted with no code
change.

The second instance, weeks later, claimed that *nested* coroutines
--- one coroutine's body creating and resuming another --- destroyed
the inner coroutine after its first yield.  Several reproducers
were drafted; all failed with "invalid coroutine" or "no such
command."  Every one was mis-counted.  The `coroutine` command runs
its body to the *first* yield at creation time (Tcl semantics), so
a body containing a single `yield` suspends once during creation
and is resumable exactly once more before it returns and its
command is auto-deleted.  The reproducers resumed the coroutines
two and three times and read the correct "you have exhausted this
coroutine" error as corruption.  An instrumentation trace of the
create-and-resume ordering showed the inner coroutine being
created, resumed, and completed correctly inside the outer --- the
feature worked.

What makes this case study more than a repeat of Section 6.17 is
what the second investigation found *underneath* the false
reproducer.  Correcting the yield count did not simply make the
tests pass; it exposed a genuine defect the noise had been hiding.
The interpreter tracks "which coroutine does a bare `yield`
suspend" in a single slot, and that slot was being *cleared* rather
than *restored* when a coroutine activation returned.  So an outer
coroutine that created or resumed an inner one could no longer
yield afterward: its own `yield` reported "yield can only be called
inside a coroutine."  This was real, and it had never been caught
because nested coroutines had no test at all.  The fix saves the
enclosing coroutine's slot on entry and restores it on exit.

Proving that fix load-bearing mattered especially here, because the
surrounding machinery had already been damaged once in this arc by
an over-eager change to cancel-unwind handling that had to be
reverted.  Rather than trust that the new tests passed, we ran a
*surgical revert-test*: revert one of the restore sites to its
broken form, rebuild, and confirm that exactly one new test --- the
one exercising an inner coroutine that *completes* before the outer
yields --- fails with the exact predicted error, then restore it.
That converts "the fix is correct" from an assertion into an
observation, and it distinguishes a load-bearing line from a
cargo-cult one.

The lessons are general:

1.  **A reproducer is a claim that must itself be verified.**  Before
    a failing script is accepted as evidence of a defect, its
    *premises* must be checked with a smaller passing probe: how
    many times does this body yield?  What does the construction
    command itself return?  For coroutines the trap is that
    creation consumes the first yield; the analogous trap exists
    wherever an operation has hidden state transitions.  The probe
    is one line and it is not optional.

2.  **Mis-counted state transitions are the dominant false positive
    in stateful subsystems.**  Across two independent investigations
    of the same machinery, the reproducers failed for the same
    reason --- reasoning about resume/yield counts without pinning
    the exact semantics first.  When a multi-step script over a
    state machine fails, the base rate favors a counting error in
    the script over a defect in the machine.

3.  **False reproducers can still sit on real bugs.**  The correct
    response to "this reproducer is flawed" is not "there is no
    bug" but "re-derive the reproducer correctly and look again."
    The genuine yield-prompt defect surfaced only after the
    mis-counting was removed; dismissing the report wholesale would
    have left it unfixed and untested.

4.  **Prove delicate fixes load-bearing by breaking them on
    purpose.**  In a subsystem where a prior change had to be
    reverted, a passing test suite is necessary but not sufficient
    evidence that a line earns its place.  A surgical revert ---
    undo one site, watch the specific predicted test fail --- is a
    cheap, decisive check that the change is both necessary and
    correctly targeted.

### 6.25  Case Study: Coverage Negative-Arms as a Bug-Finding Tool

TH8's release gate targets a flat 95% MC/DC (section 4.13).  At
~87% the residual is dominated by *error and negative arms* --- the
`goto done` cleanup that a decrypt-failure takes, the guard that
rejects a malformed input --- which normal runs never execute.  The
comfortable reading of an uncovered defensive branch is "untested,
but presumably correct."  Driving one of these arms to close a
coverage gap turned that reading inside out.

The lever was a set of crypto tamper / signature-failure fixtures
for the `[secure]` variable persist path (section 4.10), which
stores an AES-256-GCM blob under a potentially untrusted key-value
store.  A length-preserving test primitive (`::th8testlib::kv set`)
lets a test plant a *crafted-bad* blob under the secure key prefix
and then call `secure load`, asserting the specific validation error.
Five arms were targeted: a corrupted 4-byte magic, a wrong version
byte, an impossible plaintext-length field, an over-large blob, and ---
the one that matters most --- a flipped ciphertext byte, which should
trip the AES-GCM authentication tag that exists precisely to detect
tampering.

Every single case returned **success, with an empty result.**  The
five arms were at 0% MC/DC not because no test exercised them, but
because the code *silently discarded their outcome*: any assertion a
test could write would fail, so none had ever been written.  The
root cause was a fail-open status-variable defect.  `th8SecureLoad`
declared `int rc = TH8_ERROR` as its intended default, then reused
`rc` to hold the return of the key-value blob fetch.  After a
successful fetch `rc == TH8_OK`; every subsequent validation and
decrypt-failure path reached a shared `goto done` epilogue that
returned `rc` --- the stale `TH8_OK` --- and a trailing
`if (rc == TH8_OK) Th8_ClearResult()` then erased the diagnostic the
failure arm had just set.  The only guard that worked was a
too-short-blob check that happened to `return` before `rc` was
clobbered.  Authenticated encryption's entire purpose --- detecting
modification of the persisted ciphertext --- was defeated; the load
failed *open*.  The fix routed the fetch result through a separate
variable, leaving `rc` defaulted to `TH8_ERROR` so every `goto done`
fails closed; all five arms immediately began reporting their errors,
and the fixtures became their regression tests.

An audit of the file's other `goto done` funnels then found the
identical defect in `th8SecureSave` --- a failed re-encryption after a
successful decrypt returned success without persisting anything.  The
RSA sign, verify, and hash-extract funnels in the signing code, by
contrast, used the opposite idiom (a dedicated `ok` flag defaulted to
`0`, returning `ok ? TH8_OK : TH8_ERROR`) and were fail-closed by
construction.

The lessons are general:

1.  **A 0% error-arm is a question, not a checkmark.**  The default
    reading of an uncovered defensive branch --- "untested but
    presumably fine" --- is the dangerous one.  The correct reading is
    "no test has ever observed this outcome; *why?*"  Sometimes the
    answer is that the outcome does not exist.

2.  **MC/DC's observability requirement is the mechanism that finds
    the bug.**  To demonstrate a condition's independent effect you
    must make its arm produce a distinguishable result --- exactly the
    demand a swallowed-outcome bug cannot satisfy.  The coverage
    percentage is the pretext; forcing each negative path to be
    *observable and asserted* is the payoff.  This is the concrete
    dividend section 4.13 anticipated from coverage hardening.

3.  **A default-success status variable feeding a shared cleanup
    epilogue is a fail-open anti-pattern.**  The safe idiom defaults
    the status to *failure* and lets only the success path set it OK
    --- the shape the signing code already used.  Never overload that
    variable with an intermediate result whose success value survives
    into the error paths.

4.  **One funnel of this shape justifies auditing its siblings.**  The
    same author, file, and idiom produced the same defect twice; the
    fix is a two-line change but the second instance is invisible
    unless you go looking for it deliberately.

### 6.26  Case Study: A Green Build That Would Not Ship

TH8 ships as a single-file amalgamation, concatenated by
`tools/mkamal.tcl` from a hand-maintained list of source files.  Two
new units --- an allocation-site leak tracker and the compiler-runtime
stack-unwind layer added to support it --- were wired into the
per-file object build's `CORE_OBJ` and its dependency rules, and the
full test suite passed.  But the amalgamation's source list was not
updated.  The object build stayed green; only `make
amalgamation-shell` failed, at link time, with undefined symbols.  A
release cut from the amalgamation would have been broken while every
routine build reported success.

The failure mode is that a green build of one shipped artifact (the
per-file objects) does not certify a *sibling* artifact (the
amalgamation) assembled from a different, parallel manifest.  The two
manifests had drifted silently.  The sequel is the same reflex as
section 6.16: codify the invariant into a build-failing gate.
`tools/check_amal.tcl` verifies that every TH8-authored source under
`src/` is either referenced by `mkamal.tcl` or listed on a small,
documented exclude set (the shell drivers and the stubs-consumer
library, which are deliberately compiled outside the library
amalgamation).  It is compiler-free, runs in well under a second, and
is wired into the build-failing `make audit` gate, so a new file that
lands in `CORE_OBJ` but not the amalgamation list now fails the build
instead of the release.

A sibling gate from the same period enforces a documentation
convention rather than a build manifest.  The project's rule --- every
function definition immediately preceded by a Tcl-style `* Name --`
banner carrying its Why/How --- had decayed into grouped and missing
headers as the code grew.  `tools/check_headers.tcl` makes the
convention build-failing (with a `CHECK-HEADERS-OK` escape marker for
the handful of cases its parser cannot see through); a one-time sweep
brought the tree to zero violations across roughly 1,600 functions,
and new drift now fails `make`.

The lessons are general:

1.  **Every shipped artifact needs its own gate.**  An amalgamation, a
    stubs library, a Windows build --- each is a distinct manifest that
    can drift from the canonical object build.  "The tests pass"
    certifies only the artifact the tests ran against; a second
    artifact assembled from a second manifest is unverified until
    something builds *it*.

2.  **The codification reflex generalises beyond coding style.**
    Section 6.16 turned a manual anti-pattern audit into a static
    checker; the same move applies to build-manifest integrity and to
    documentation conventions.  Any invariant a human currently
    maintains by discipline is a candidate for a build-failing gate,
    and the gate earns its keep the first time the discipline visibly
    fails --- which, for both of these, is exactly when it was written.

---

### 6.27  Case Study: An Innocent Test and the Depth of a Root Cause

The most expensive investigations often begin with the least
threatening test.  `apicontract-9.1` --- named `env_stress` ---
exists only to confirm that the environment-variable backend
serialises concurrent access with a mutex: it spawns eight worker
threads that hammer the same `::env` key and checks that all 1600
set/unset cycles complete.  It had passed on macOS for months.  On
a fresh Linux host it failed with a diagnostic that had nothing
obviously to do with environment variables: *"C stack overflow
(TH8 stack limit reached)."*

The first temptation was to file it as flaky and move on --- the
failure was platform-specific and looked timing-related.  Taking it
seriously instead led through four layers, each deeper than the
last.

1.  **The symptom.**  TH8's C-stack guard computes
    `nUsed = |pStackBase - &localMarker|` and compares it against a
    recorded stack size.  On the failing runs it computed *gigabytes*
    of apparent usage.  The reason was not a runaway recursion: the
    stack *base* had been captured on the main thread at
    `Th8_CreateInterp` time, but the offending `Th8_Eval` ran on a
    worker thread.  glibc `mmap`s each pthread stack far from the main
    stack, so subtracting a worker-stack address from a main-stack base
    yields a nonsensical multi-gigabyte span.  macOS's stack layout
    happens to place them close enough that the bogus arithmetic stayed
    under the limit --- the bug had been latent, hidden by platform
    luck, the whole time.

2.  **The root cause.**  The test was creating an interpreter on one
    thread and using it on another.  TH8's specification (§37a) already
    stated that the API surface is single-threaded per interpreter ---
    but the contract lived only in prose.  Nothing *enforced* it, so a
    test could violate it and the violation could hide behind a
    platform's memory map until the day it did not.

3.  **The correctness fix.**  The immediate repair was to rework the
    test so each worker builds and destroys its *own* interpreter on
    its own thread --- the pattern the contract requires --- followed by
    an audit of every `pthread_create` site to confirm no other code
    made the same mistake.  But a passing test is not the same as an
    enforced invariant.  The durable fix was to convert the prose
    contract into an executable one: capture the owning thread's id in
    `Th8_CreateInterp` and assert, in debug builds, that the most
    important owning-thread-only entry points --- the evaluation choke
    point, teardown, language registration, and the variable / result /
    command surface --- are only ever called on that thread
    (`TH8_ASSERT_OWNER`).

4.  **The API refactor.**  Enforcement needed a public way to read the
    owning-thread id (`Th8_GetInterpThreadId`), and reading it safely
    from a foreign thread needed an *atomic* 64-bit load.  The platform
    abstraction offered only a 32-bit compare-exchange
    (`xIntCmpXchg`) --- too narrow to hold a thread id.  Closing the gap
    meant adding a 64-bit sibling primitive (`xIntCmpXchg64`, exposed as
    `Th8_Int64CmpXchg`) across every platform table in lockstep, and ---
    being still pre-RTM --- collapsing the platform ABI version back to
    1 rather than carrying a bump.  A one-line concurrency test had, by
    this point, produced a documented bug fix, a new class of debug
    assertion, two new public APIs, and a minor ABI change.

The generalisable lessons:

1.  **An innocent test is a probe into unstated invariants.**  The
    value of `env_stress` was never the environment backend; it was
    that concurrency exercises assumptions the single-threaded tests
    never touch.  A failure with a symptom unrelated to the test's
    stated purpose is a signal that the test has wandered into
    unspecified territory --- exactly where the interesting bugs live.

2.  **A contract that lives only in prose will be violated, and the
    violation surfaces on the least forgiving platform.**  This is the
    same codification reflex as Sections 6.16 and 6.26: any invariant a
    human maintains by discipline is a candidate for an executable
    gate.  Here the gate is a debug assertion rather than a build-time
    checker, but the move is identical --- and, as always, it earns its
    keep the moment the discipline first visibly fails.

3.  **Root causes have depth, and stopping early is the real cost.**
    The retry-and-forget path would have left a latent memory-map
    landmine, an unenforced contract, and a missing platform primitive
    all in place.  The distance from *"one flaky test"* to *"a new
    platform-abstraction primitive"* is a feature of taking the first
    step, not an accident of this particular bug.

---

### 6.28  Case Study: When You Find a Hole, Keep Digging

A user on a fresh Linux box reported libunbound errors printing to the
terminal:

    error opening file /etc/unbound/root.key: No such file or directory
    ...
    module init for module validator failed

The cause looked immediate.  TH8's shared DNSSEC resolver
(`th8UnboundResolve`) enabled libunbound's validator module but, when no
trust anchor could be found, left it enabled with nothing to validate
against; the module's init then failed against a missing key file.  The
resolver even *documented* a "no-anchor -> validation disabled" mode that
had never been implemented.  Implementing it --- disable the validator when
no anchor loads, so resolution degrades cleanly to insecure --- was a real,
correct fix.  It built clean.  It matched the symptom exactly.

It was also the wrong fix, and the next three turns were spent learning
that.

1.  **The plausible fix that changed nothing.**  The user rebuilt and
    reported: *still seeing it.*  The temptation was to insist the fix was
    correct (it was) and blame a stale build.  But a fix that provably
    addresses a mechanism, yet does not move the symptom, has not been
    connected to the symptom --- only to a mechanism that *resembles* it.

2.  **A green check that never ran.**  Local attempts to reproduce had all
    printed "0 spew lines," which had read as confirmation.  They were
    worthless: the test build installs a signed-only script policy, and it
    was silently rejecting the unsigned scratch scripts with "couldn't
    retrieve" --- every probe had exited without executing a line, and
    "0 spew" was counting the output of a program that never ran.  A pass
    that was never observed to be capable of failing is not evidence.

3.  **The machine that could not reproduce the bug.**  Even once the
    scripts were signed and running, the symptom would not appear ---
    because the dev box (macOS) *had* `/etc/unbound/root.key`, libunbound's
    compiled-in fallback.  "Correct by construction" had been standing in
    for a result that the environment was structurally incapable of
    producing.  The box that cannot reproduce a failure cannot confirm its
    fix.

4.  **The actual hole, one layer down.**  The user's own suite output
    pinned it: the spew appeared amid the `clock` tests.  There were *two*
    independent libunbound integrations in the tree.  The first fix had
    been applied to the general resolver; the terminal spew came from the
    *other* one --- a hand-rolled `ub_ctx` inside the NTP/`clock` code that
    built its own context, chained hard-coded trust-anchor paths, and ---
    unlike the shared resolver --- never called `ub_ctx_debugout()` to
    silence libunbound's stderr.  Pointing its hard-coded paths at
    nonexistent files finally reproduced the exact error on the dev box;
    the fix silenced it; a surgical revert restored it.  Load-bearing, at
    last, and proven so.

5.  **The hole was structural, not textual.**  The durable repair was not a
    second patch bolted onto a duplicate.  One external dependency had two
    integrations, only one of them hardened; that *is* the defect.  The
    real fix deleted the duplicate --- routing the `clock` path through the
    same shared, hardened resolver --- so the class of bug (a second copy
    drifting out of sync with the first) cannot recur.

The generalisable lessons invert a familiar saying.  *When you are in a
hole, stop digging* is good advice about sunk cost.  It is terrible advice
about diagnosis, where the first firm floor you hit is usually a false
bottom.

1.  **A plausible fix that does not move the symptom is a hypothesis, not a
    conclusion.**  The mode-(d) fix was correct and even shipped --- but it
    was not the reported bug.  Matching the mechanism is not the same as
    matching the failure; only the symptom moving proves the connection.

2.  **Verify the verification.**  A green result is only evidence if the
    check could have gone red.  A harness that silently no-ops --- a
    rejected script, a skipped constraint, a swallowed exit code --- turns
    every subsequent "pass" into noise dressed as signal.  Before trusting
    a clean run, confirm the run ran.

3.  **The environment is part of the experiment.**  A development machine
    that differs from the failing one in the exact dimension under test
    (here, the presence of a fallback key file) cannot reproduce the bug
    and therefore cannot confirm the fix.  Reproduce first; "correct by
    construction" is a plan for a test, not a substitute for one.

4.  **Duplication is where the second bug hides.**  When two copies of a
    thing exist and only one was hardened, the fix is rarely a third
    patch --- it is removing the copy.  The last hole in this dig was not a
    missing call; it was a missing abstraction.

### 6.29  Case Study: Testing a System in Its Own Terms

A late addition to the harness was env-driven subset selection: four
environment variables (`TH8_TEST_MATCH`, `TH8_TEST_SKIP`, `TH8_TEST_FILE`,
`TH8_TEST_NOTFILE`) --- the env-driven equivalents of tcltest's `configure
-match/-skip/-file/-notFile` --- so a critical subset could be run under
Valgrind without editing the master file list.  File-level selection was
trivial: `runAllTests` already iterates file names as strings.  Test-level
selection needed one datum, a test's name, and the obvious way to get it was
wrong.  The ways it was wrong are the lesson.

1.  **Parsing the system's data from outside.**  Each test is written
    `runTest {test NAME {desc} -body {...} -result {...}}`, so the name is
    the second word and `[lindex $script 1]` appears to read it.  It did ---
    for 4,405 tests.  Then three files errored out of the suite entirely,
    with an empty message.  The culprits were the backslash-escape torture
    tests (`coverage_backslash_escapes.tcl` and kin), whose bodies contain
    deliberately malformed constructs such as `"\u12Z"`.  Those bodies are
    valid Tcl *scripts* but not valid Tcl *lists*, so `[lindex]` --- which
    must parse its argument as a list --- threw before ever reaching index
    one.  The harness had reimplemented, badly, a parse the system already
    performs correctly: the `[test]` command binds `NAME` as its first
    parameter regardless of whether the body is a well-formed list.  The fix
    was to stop parsing from outside and let the system parse --- shadow
    `[test]` with a stub that records its first argument, evaluate the
    script to trigger it (the braced body is an unevaluated argument, so
    nothing runs), then restore.  *To observe a system correctly, use the
    system's own primitives; an outside-in re-parse is a second
    implementation that inherits none of the original's edge cases.*

2.  **Names have a namespace; do not assume the alias.**  The first stub
    shadowed `::test`, the global name.  But the harness lives in
    `::th8test` and merely *imports* its commands into `::` for the test
    files' convenience.  Reaching for `::test` from inside `::th8test` bakes
    in an assumption --- that the global import has run --- that a package
    has no business making about its own callers.  The correct target is the
    package's own command, resolved relative to the current namespace:
    `rename test __saved` (resolving `test` in `::th8test`), with the
    capture evaluated in `[namespace current]` so the script's `[test]`
    resolves to the stub.  A command's real name is the one in the namespace
    that owns it, not the alias a caller happens to see.

3.  **Know your alias semantics before you rename through them.**  Momentary
    alarm followed: after the rename dance, probes reported `test` and
    `runTest` as "no such command," which read as a corrupted command table.
    It was nothing of the kind.  Every probe was a fresh, isolated process,
    and the true cause was one layer down --- editing the (signed) harness
    file without re-signing it had left its signature stale, so the
    signed-only loader silently refused to load the harness *at all*, and
    "no such command" was, once again, the output of a program that never
    ran (§6.28's lesson in a new disguise).  Re-signed and actually loaded,
    the mechanism proved clean: rename of an imported command round-trips
    with full fidelity, the import binding survives a rename of its origin
    (it binds to the command *object*, not the name), and the capture does
    not double-execute the real test.  The frightening reading was an
    artifact of an experiment whose subject had never been instantiated.

The generalisable lessons:

1.  **Test a system in its own terms.**  When you need a datum the system
    already computes --- a parsed name, a resolved type, a normalized
    path --- get it from the system, not from a private re-derivation.  The
    re-derivation is an unmaintained second implementation that will diverge
    on exactly the inputs the tests were written to stress.

2.  **Resolve names where they are defined, not where they are imported.**
    Code inside a namespace should name its own commands relative to that
    namespace; leaning on a global alias couples the callee to an import its
    callers might never have performed.  Aliases are a convenience for the
    caller, not an interface for the callee.

3.  **Understand your alias and import semantics empirically.**  Renaming,
    shadowing, and restoring commands through an import layer is safe only
    once you know how that layer binds --- to a name or to an object, and
    whether the binding survives a rename.  Verify it on the real system;
    intuition inherited from a sibling language is a hypothesis, not a
    guarantee.

4.  **When the table looks corrupt, check whether the program loaded.**  A
    cascade of "no such command" from a fresh process is far more likely to
    be a harness that failed to initialize --- a rejected signature, an
    aborted source, a missing package --- than genuine corruption.  Confirm
    the load succeeded before diagnosing the damage.

### 6.30  Case Study: A Bug the Optimizer Hid (Bug 76)

The env-driven subset work of §6.29 surfaced a second, unrelated defect while
it was being tested: three conformance files errored out of the suite with an
*empty* message.  The proximate cause was the outside-in `[lindex]` parse of
§6.29 --- but the reason its error was *empty* rather than "unmatched quote"
was a separate bug that turned out to be one of the strangest in the project.

Reduced, `llength {x "Z"]}` (a quoted element followed by a non-space) returned
an error whose **message was the empty string** instead of `list element in
quotes followed by "]" instead of space`.  Every symptom argued *against* it
being a real bug:

* The list parser is a mature, vendored component; source review, a Coverity
  run, and the project's own fuzzers had all found nothing.
* It would not stay reproduced.  A clean **release** build printed the correct
  message; a clean **debug** build printed nothing, deterministically; and
  adding a single `fprintf` near the failure sometimes flipped the outcome.

Those are the classic signatures of undefined behavior or a stale build, and
the investigation nearly closed with the wrong verdict twice --- first "stale
object" (refuted: the dependency guard was green), then "va_list forwarding
through the platform-callback chain" (refuted: a direct `vsnprintf` call, one
hop, was *also* empty in debug).  What finally settled it was refusing to
reason and instead **instrumenting the exact data**: in the debug build, a
`va_copy` inspection showed the arguments were perfect (precision `1`, a
pointer to the offending `']'`), `vsnprintf` had returned `-1`, and the format
string was `... followed by "%.*ls" %ls` --- the **wide** `%ls` conversions.

TH8's `se_WCHAR` is a single-byte `unsigned char`; handing that narrow string
to `%ls` makes `vsnprintf` fail and emit nothing.  The format was chosen by a
preprocessor conditional, `#if defined(USE_NARROW_CHAR_T)`, positioned in the
override header **thirty-seven lines before `USE_NARROW_CHAR_T` was
`#define`d**.  So the selection could bind to the wide branch; whether the
final translation unit bound narrow or wide then depended on include-order and
macro-redefinition interplay that differed between build configurations.
Release happened to bind narrow and hid the bug; debug bound wide and exposed
it.  The one-line fix moves the selection after the definition.

The lessons generalise past this one macro:

1.  **A "cosmetic" symptom is still a symptom.**  An empty error message reads
    as a trivial blemish, and the first instinct was to route *around* it ---
    §6.29 replaced the name capture with a `[test]` stub, sidestepping the
    throwing `[lindex]` entirely and making the blank message stop mattering.
    That blank string was in fact the visible tip of a compile-time
    undefined-behavior defect that mis-set *every* argument-bearing list-parse
    diagnostic in an entire build configuration.  It took an outside prompt
    ("an empty error message is a big no-no --- is it real?") to convert a
    nuisance into an investigation.  A dropped field, an off-by-one in output,
    a message that should not be empty: treat the anomaly as a lead, not a
    blemish to paper over.  The distance between "cosmetic" and "corruption"
    is often a single afternoon of digging.

2.  **A compile-time-selected bug has a tooling blind spot.**  Coverity and
    fuzzing see one build's preprocessor output.  A macro *used before it is
    defined* is a latent defect even when the build "happens to work," and no
    amount of analysis of the *working* build will reveal it.

3.  **A debug-only heisenbug is not automatically a build artifact.**
    Optimization sensitivity and undefined behavior share the "vanishes on
    rebuild, flips with a print" signature.  Reproduce it in the
    configuration where it is *stable* --- here the unoptimised debug build ---
    and instrument *there*.

4.  **Instrument the data; do not infer it.**  Reading the source "proved" the
    two call paths were identical.  Only printing the actual `vsnprintf` return
    value, the live `va_list` arguments, and the resolved format string made
    the wide/narrow mismatch --- invisible in the C source --- undeniable.

5.  **Every dismissal is a hypothesis with a test.**  "Stale object" was
    falsified by a green dependency guard; "va_list chain" by a one-hop direct
    call.  Writing down what each theory *predicts* and then checking it is
    what keeps a heisenbug investigation from converging on a comfortable
    non-answer.

| Metric | Value |
|--------|-------|
| C source lines | ~71,000 code lines (amalgamation, excluding comments and blanks) |
| Source files | 65 (core + plugins + platform + crypto + test) |
| Normative requirements | 834 (language standard) + 260 (language extensions) + 248 (C API spec) + 13 (internal API spec) = 1,355 raw across all four; 1,151 unique |
| Test coverage | 2,511 R-marker invocations across 87 top-level files (103 including subdirs); 1,134 covered markers (98.52%); 0 orphans |
| Test pass rate | 100% (4,445 passed / 0 fail / 6 skipped / 0 mutated as of 2026-08-07) |
| Conformance profiles | 3 (Core, Standard, Full) |
| Independently gateable plugins | 16 top-level + crypto + regexp subdir plugins |
| Compile-time feature gates | 3 core + 14 plugin + 4 optional |
| Platform layers | 11 (libc, null-I/O, POSIX, Win32, macOS, curl, mimalloc, cosmopolitan, env, mem, fault) |
| Platform callbacks | 66 (plus pCtx) |
| Policy callback | 1 unified (Th8_PolicyProc with phase bitmask) |
| Fuzz harnesses | 6 (eval, expr, list, format, harpy, snk) |
| Fuzz regression corpus | 24 crash inputs (list parser) |
| Fault injection tests | 19 (systematic allocation failure sweep) |
| Math functions | 60 (12 core + 21 transcendental + 20 C99 + 7 classification) |
| Static-audit rules | 39 (overflow safety, libc bypass, stdio/time/threading layer discipline, naming hygiene, ABI version, public-header purity, CERT-C / CWE-Top-25 banned-function family) |
| Known open bugs | 0 |
| Registered resolved issues | 18 |

TH8 builds and passes its full test suite on Linux (x86-64,
aarch64), macOS (Apple Silicon), and Windows (x86, x64) under
GCC, Clang, and MSVC.  The amalgamation format (a single `th8.c`
plus `th8.h`) simplifies integration into host applications.
The public header chain requires no platform-specific system
headers (`<windows.h>` is not included; `CRITICAL_SECTION` is
defined inline in `th8_plat.h`).


### 6.31  Case Study: A Flaky-Looking Test That Unmasked a Security Weakness

The most consequential security hardening in this project did not begin as a
security task.  It began as a single test — `coverage8-10.1`, a `clock ntp`
check — failing *only* on the Linux CI runner, twice in a row.  The reflexive
diagnosis was "network flake."  It was not.

Pulling the thread revealed a real, deterministic defect (documented as Bug
78): the NTP client resolved the server with `getaddrinfo(AF_UNSPEC)` but only
ever contacted the *first* address it returned.  glibc orders IPv6 first
(RFC 3484), and the Linux runner had no working IPv6 egress, so the client
dead-ended on a black-holed address and never tried the reachable IPv4 ones —
while silently defeating the round-robin server's fault tolerance.  macOS
ordered the addresses differently and "worked," which is exactly what made it
look like a flake.

Fixing that meant reading the *adjacent* resolution and validation code — and
that reading is where the real discovery happened.  The DNSSEC validation we
believed protected `clock ntp` turned out to be a **gate that was decoupled
from the connection**: it validated the name, checked only a `bogus` verdict
(never a positive `secure` one, so an *unsigned* or *no-anchor* answer sailed
through), then **threw the validated addresses away** and re-resolved via an
unvalidated `getaddrinfo` for the actual socket — over IPv4 records only, while
the connection could use never-validated IPv6.  Every individual piece was
present; none of it was load-bearing.  The code was not as secure as everyone,
including its authors, assumed.

So a one-line test failure became an end-to-end hardening: return a `secure`
flag from the resolver, **require** it, connect only to the DNSSEC-validated
A *and* AAAA addresses, bundle a signature-verified IANA root trust anchor so
validation works out of the box, and require any operator-supplied anchor to
be signed as well.  And in *verifying* that, one more layer peeled back: the
DNSSEC-signed mirror we switched the default to was fronted by a CDN that does
not carry NTP, so the now-correct client correctly reported that a signed name
pointed at hosts that could not answer — a deployment truth the old,
never-really-validating client had been too lax to expose.

Then the hardening bit back.  With the validating path in place, the client
began failing with the *generic* quorum message ("insufficient server
responses").  On request we **un-masked** it — made the multi-server layer
preserve the specific per-server diagnostic instead of overwriting it — and the
un-masking paid for itself within a single command: the real error was "cannot
securely resolve," for *every* name, including `iana.org`, on a host that could
otherwise do DNSSEC.  A fail-**closed** security check had turned a broken
*dependency* into what looked like a security *refusal*: "this name isn't
secure" actually meant "our resolver can't resolve anything."  When a
fail-closed path fires on every input, the mechanism is the suspect, not the
inputs.

The mechanism was our own hardening.  Two options we had enabled "to be safe"
were incompatible with reality.  `harden-referral-path` — experimental and
non-RFC — issues extra infrastructure queries that SERVFAIL in a one-shot
embedded resolver with a small outgoing-range: total failure.  `use-caps-for-id`
(DNS 0x20 case randomisation) was rejected by Cloudflare, which fronts the
signed name and does not preserve query-name case: **intermittent** failure, a
different shape that pointed at a per-server-path option rather than a universal
one.  Because the embedded library was (correctly) silenced, both surfaced only
as "no addresses."  The diagnosis came from reproducing *outside* the silence —
stock `unbound-host` validated the same name with the same anchor — and then a
standalone context harness with leave-one-out bisection that *measured* each
option's failure rate (7/12 with 0x20 on, 12/12 off) instead of reasoning about
whether it "should" be safe.  Neither option was load-bearing: DNSSEC validation
is the actual anti-spoof, so removing them cost no security and restored
resolution.  A last thread, pulled by the same "don't assume" instinct that
started the arc: the client had been willing to try an IPv6 address on a host
with no IPv6 egress — so it now probes each resolved address's routability with
a packet-free `connect()` and assumes *neither* family reachable.

The lessons compound.  A test that fails "only sometimes, only on one platform"
is a hypothesis about an address-ordering or environment divergence, not a coin
that landed badly — treat it as signal.  A bug investigation is a licence to
audit its neighbourhood: the surrounding code is exactly where latent security
assumptions go unchallenged precisely because nothing has forced anyone to look
at it.  Hardening is not monotonically safer — an option that is real hardening
in a full recursive server can be a total or intermittent outage in an embedded
stub or against a specific provider, so each must be verified against the actual
deployment shape, not its name.  And a fail-closed guard can *disguise* a broken
dependency as a policy refusal, so keep the most specific diagnostic and, when a
silenced library "returns nothing," reproduce outside the silence before
blaming the inputs.  The failing test did not just have a fix; it had a security
review — and a resolver-reliability review — attached, if we were willing to
keep reading.


### 6.32  Case Study: What "All-Or-Nothing" Actually Means

A project-wide rule was adopted mid-development: every state-mutating API must be
*transactional* — on any failure it rolls back all incomplete changes and leaves
state exactly as before.  The first API put under that lens was `Th8_CreateInterp`,
via a new test that swept a one-shot allocation fault across the whole constructor
and asserted the obvious-looking invariant: **whenever the fault fired,
construction must return NULL.**  The test failed immediately and dramatically —
of 152 construction allocation trips, 62 fired the fault yet still returned a live
interpreter.  Read literally, that said the constructor tolerated 62 allocation
failures, and the natural next step was a large refactor to make every one of them
fatal.

That refactor would have been a mistake, and the reason is the whole lesson.
Backtracing the 62 survived trips showed every one routing through the same place:
the interpreter builds two list-valued globals (`tcl_platform(source)` and
`(compileOptions)`), and the list/string machinery consults the
internal-representation cache to memoise the parsed form.  The cache — by explicit
contract — returns `NULL` on out-of-memory, and every caller is written to proceed
with the correct, uncached value.  So a fault on a cache-warming (or transient
scratch) allocation resumes, the list is built correctly, the global is set
correctly, and the interpreter that emerges is byte-for-byte complete, differing
only in that a cache is cold — a state it would have occupied anyway before its
first use.  None of those 62 "tolerated failures" left any observable residue.

The defect was in the invariant, not the constructor.  "The allocation failed
somewhere inside the call" is not the same event as "observable state is
truncated," and a transactional gate that conflates them will fire on every
optimisation a system deliberately degrades under memory pressure.  The corrected
test asserts the *right* property — **complete-or-nothing**: on every fired trip,
the constructor must return either NULL or a *fully complete* interpreter, and it
verifies completeness by introspecting observable state (both list globals have
their exact element counts, a global can be set and read back, a command can be
registered).  It passes with zero violations, which is a stronger and truer result
than the original: not "faults yield NULL" but "no published interpreter is ever
partial."  This mirrors, from the opposite side, an earlier lesson that a resource
test must assert the *quantity* and not a mere inequality (§6 on the sandbox step
counter): there the proxy was too weak and missed a real gap; here the proxy was
too strong and manufactured a phantom one.  Both fail for the same reason — they
check something adjacent to the property instead of the property.

The audit was not fruitless.  The same sweep surfaced one genuine gap the cache
noise had to be cleared away to see: secure-variable initialisation, whose
allocation failure was silently discarded, producing an interpreter that lacked
secure-variable support without saying so.  Unlike a cold cache, missing a
*capability* is observable state, so that failure was made fatal.  The discriminator
that separated the 62 false alarms from the one real bug is exactly the one the
project rule now codifies: does the incomplete change alter anything a script can
see?  Caches, lookaside pools, and scratch buffers do not and need not roll back; a
missing command, namespace, variable, channel, or capability does and must.  The
rule is not "never tolerate an allocation failure" — it is "never let one become
*visible*."

The generalisable lessons.  A transactional invariant has to name the state it
governs, and the right boundary is observability, not allocation: internal,
idempotent state (a memoisation cache) is a valid complete state even when cold, so
demanding its rollback is not stricter correctness, it is a robustness regression
that fails operations the system could have survived.  When a plausible invariant
makes a correct system look broken at scale (62 of 152 trips), suspect the
invariant before the system — and let the counter-examples classify themselves: the
backtrace that keeps landing in the same failure-tolerant helper is telling you the
allocation was optional.  Finally, a too-strong gate is not harmless: it would have
driven a large, actively harmful refactor, and only reclassifying the 62 as benign
kept the real one-line capability bug in view.


### 6.33  Case Study: You Cannot Test a Rollback by Re-Running One-Shot Setup

The companion to §6.32 concerns the *other* half of transactional construction:
the process-global `Th8_Initialize`, whose two failable lifecycle callbacks
(`xInitialize`, `xSetCwd`) must, on failure, undo every completed stage in reverse
order.  The natural test writes itself: fail the callback, assert the init failed,
then call `Th8_Initialize` again and assert it now *succeeds* — a clean recovery
proving the rollback left no leftover state.  It crashed on the second init, deep
in the allocator's per-thread setup.

The crash was not the rollback's fault, and understanding why it wasn't is the
lesson.  `Th8_Initialize`'s rollback is written to mirror `Th8_Finalize`, and
`Th8_Finalize` unregisters the *main* thread from the allocator.  So the rollback
does too — and this allocator's main thread is special: once torn down, it cannot
be re-initialized in the same process, so a `Th8_Initialize` / `Th8_Finalize` /
`Th8_Initialize` cycle crashes just as surely as the failure path did.  The library
is once-per-process by contract; the "recovery" oracle was asserting a capability
that does not exist, and its crash carried no information about whether the undo was
correct.

The generalisable point is a testing one: **a rollback defined as the inverse of
teardown cannot be validated by re-running setup, when teardown is itself
one-shot.**  Inferring "the undo worked" from "a later redo succeeded" only holds if
redo is supported; when it is not, the inference is vacuous at best and, as here,
crashes on a path unrelated to the property under test.  The oracle must observe the
undo *directly*.  The working harness instruments each lifecycle callback with a
counter that chains to the real implementation, runs exactly one failing init, and
reads the counts: the earliest failure leaves every later stage untouched (nothing
to undo), while the deepest failure shows each completed stage — the initialize
hook and the mutex stage — undone exactly once, in reverse order.  No
re-initialization is involved.

The investigation did not merely leave a sharp edge documented; it turned one up
and fixed it.  Building the "recovery" oracle is what first crashed the library on a
second initialization, and although that oracle was the wrong way to *verify the
rollback*, the crash it exposed was a genuine public-API defect: a library that
SIGSEGVs rather than errors when re-initialized.  The root cause was the
initializer's teardown calling the allocator's per-thread `thread_done` on the main
thread, which frees thread-local state the allocator keeps alive until process exit
(worker threads are reclaimed by a thread-exit hook; the main thread is not).  Both
the finalizer and the failed-init rollback ran on the main thread, so a subsequent
initialize dereferenced freed state.  The fix restricts `thread_done` to confirmed
worker threads.  With it, the same `recover` (failed-then-clean init) and `reinit`
(repeat init/finalize) sequences that crashed now succeed, and reverting the guard
reproduces the crash on demand.  The compounded lesson: a bad oracle can still be a
good bug detector -- the discipline is to separate the two, keep the real fix, and
discard only the flawed inference.  "The contract says once per process" is a
description of intended use, not a licence to crash when the use is stretched.


### 6.34  Case Study: One Owner for an Invariant, and a Test That Measured Absence

A sandbox memory limit is only as strong as the paths that respect it.  TH8's
per-interpreter allocation limit was enforced on the normal path, but a
"second-chance" recovery hook -- invoked when an allocation fails so the embedder
can free caches and retry -- could satisfy a request the limit had just refused,
allocating straight past the ceiling.  The defect and its fix are a small lesson in
where an invariant should live, and the test that "verified" the fix is a sharper
lesson still.

The invariant -- check the limit, then account the bytes -- was implemented in two
places: the core allocator and the second-chance path.  The second copy accounted
the recovered block but omitted the limit check (and double-counted against any
caller that had already accounted).  Two hand-maintained copies of one rule drift,
and they drift first exactly where coverage is thinnest.  The repair was not to add
a third check inside the recovery hook -- which would have been a third copy, this
time inside embedder-supplied code that cannot even see the internal counters -- but
to collapse to a single owner: the core allocator does the limit check, the
zero-fill, and the accounting, once; the recovery path reclaims memory and then
routes the retry back through that same core.  With one owner, "the recovery path
respects the limit" is true by construction rather than by vigilance, and the
callback contract can stay simple: return a block the core will use as-is, obtained
by delegating to a real allocation entry point (so it is accounted) -- or a raw
block from a private reserve, explicitly opting that block out.  A recovery hook is
not a policy escape hatch; freeing caches to make a request fit is legitimate,
answering a policy rejection with a raw allocation is not, and re-entering the one
checked path is what distinguishes them.

Then the test.  The first version created child interpreters, set a tiny limit,
asked for far more, and confirmed the request was refused -- green.  It was green
because the children had no recovery hook at all: the default platform does not
install the memory-recovery layer, so there was nothing to bypass and nothing to
test.  The check was measuring the *absence* of the feature, not the *presence* of
the fix.  The tell was quiet but unmistakable in hindsight: applying and reverting
the fix produced identical results.  A change that does not move its own test is a
test that is not reaching the changed code.  Once the test explicitly installed the
recovery layer onto the child -- opting into the exact configuration the finding is
about -- the path finally ran, and reverting the fix flipped the outcome.  Only then
did the test discriminate between the fixed and broken code, which is the only
property that makes a regression test worth keeping.  The generalisable pair: put a
cross-cutting invariant in one place so it cannot be half-applied, and make a test
fail before you trust it to pass -- especially a test whose subject is an optional
capability the default build leaves switched off.


### 6.35  Case Study: Designing Out a Race Instead of Locking Around It

`Th8_CancelEval` is the interpreter's one cross-thread lever: any thread, or a
signal handler, may ask the running script to stop.  Getting its concurrency
right went through three shapes, and the arc is instructive precisely because the
middle shape *looked* finished.

The original code stored the cancel request as several ordinary `volatile`
fields -- a message pointer, a length, a flags word -- and set an atomic bit last.
`volatile` is not synchronisation in C, so two foreign cancellers could tear the
pointer/length pair and the owner could observe a pointer from one publisher with
a length from another.  The audit flagged it as a real data race.

The second shape wrapped the whole multi-field request in a bounded lock-free
critical section: publishers wrote the request under a CAS try-spinlock, and the
owner adopted it under a try-acquire that never blocked the evaluator.  It passed
the existing contract test and a first ThreadSanitizer run.  It was still wrong,
and the thing that exposed it was strengthening the *stress* test rather than
writing new code.  The audit's standing charge against the old evidence -- that a
test which spawns one worker, joins it, then inspects state proves "no crash," not
"no race" -- applied verbatim to our own test.  Once the stress test actually
drove foreign non-signal messages with the allocator's own instrumentation
enabled, TSan pointed at the byte-accounting counters.  The lock had guarded "the
request," but the publish path also *touched* the owner-only allocation
accounting, because the foreign thread built its message buffer through the
accounted allocator.  A critical section guards what you put inside it; it does
nothing about the side effects you forgot were part of the operation, and the
mere presence of a lock invites the belief that the whole operation is covered.

The third shape removed the lock entirely by changing the *shape of the data* so
there was nothing left to serialise.  Two moves did it.  First, the entire request
STATE -- the canceled bit and every flag -- collapsed into a single machine word,
OR-published atomically; a value that fits in one atomic word cannot be torn, so
there is no multi-field object to protect.  Second, the message became a
self-describing buffer, `[length][bytes][NUL]`, handed off through one atomic
pointer exchange.  Because the length lives *inside* the buffer the published
pointer addresses, no reader can ever pair one publisher's pointer with another's
length -- the original defect is impossible by construction, not merely guarded
against.  The exchange also settles ownership unambiguously: whoever swaps a
non-NULL pointer out is the one who frees it, which removes leaks, double-frees,
and use-after-free without a single reference count.

Two further points fell out of the redesign rather than being bolted on.  A
signal handler cannot allocate, so a signal-mode cancel necessarily carries no
message and publishes only the atomic word, with the interpreter reporting a fixed
"eval canceled via signal."  Being *forced* into a message-free signal path
clarified the whole precedence rule: state is always safe to publish; a message
is an optional extra the owner adopts, first-writer-wins.  And the coordination
buffer is deliberately allocated from the *raw* platform allocator, never the
accounted one -- because "which allocator" is exactly the boundary between what a
non-owner thread may and may not write.  The correctness fix even restored a
capability the "safe" middle version had abandoned: foreign threads had been
dropping custom cancel messages as un-copyable, and the pointer-swap hand-off
brought them back.  The general lesson is the oldest one in concurrent
programming, relearned concretely: when a shared object is hard to lock correctly,
the winning move is usually to reshape it into something that needs no lock -- an
indivisible word for state, a self-describing single-owner payload for the rest --
rather than to make the lock cleverer.


---

## 8.  Related Work

### 8.1  Why Not Extend Tcl?

The most natural objection is: *why not add these security features
to Tcl 8.x or 9.x via extensions?*  There are three reasons.

**The architecture prevents it.**  Tcl's `exec`, `open`, `socket`,
`glob`, and `file` commands are compiled into the interpreter core.
Safe interpreters hide these commands, but "hiding" is not the same
as "not having."  The commands exist in the same address space; a
sufficiently clever attack (or a bug in the hiding mechanism) can
re-expose them.  History bears this out: Tcl safe interpreter
bypasses have been found repeatedly over 30 years.  TH8's
interpreter literally does not link against POSIX or Win32 I/O
functions.  The capability is absent at the object-code level, not
merely hidden at the Tcl level.

**Extensions cannot change the data flow.**  TH8's preGetData and
preEval callbacks intercept data *inside* the core evaluator,
before parsing begins.  The signed-only policy fires on raw bytes
before line-ending translation.  The internal-representation cache
is integrated into the core value lifecycle.  The platform
abstraction mediates `malloc` itself.  These are not hooks that can
be bolted on from outside; they require restructuring the
interpreter's internal data flow.

**Tcl is too large to embed.**  Tcl 9.0 is approximately 200,000
lines of C.  TH8's full build is ~71,000 code lines (excluding comments and blanks), and
compile-time feature flags cut this substantially for minimal embedder builds.  Many embedding scenarios
(firmware, single-binary tools, sandboxed microservices) cannot
accommodate the Tcl runtime, its threading model, its I/O channel
layer, or its encoding infrastructure.  TH8's amalgamation format
(one `.c` + one `.h`) integrates into a host application with a
single `#include`.

### 8.2  Other Implementations

**Tcl Safe Interpreters.**  Tcl's `interp create -safe` mechanism
hides dangerous commands from child interpreters.  TH8's approach
is the inverse: capabilities are *added* rather than *removed*.
This eliminates the class of bugs where a capability is
incompletely hidden or later re-exposed.

**TH1 (Fossil).**  TH8's direct ancestor.  TH1 demonstrated the
viability of a minimal Tcl for embedding but lacked namespaces,
coroutines, packages, regular expressions, and a formal
specification.  TH1 has been deployed in Fossil for 15+ years,
processing untrusted skin templates and configuration scripts
without a single reported security incident.

**Jim Tcl.**  A small-footprint Tcl implementation with a
similar embedding focus.  Jim includes a bytecode compiler and
supports a subset of Tcl 8.5.  It does not provide a formal
specification, a platform abstraction layer, cryptographic script
verification, or compile-time modularity.

**Lua.**  The most widely deployed embeddable scripting language.
Lua's C API is minimal and its sandbox model is well-understood,
but it lacks Tcl's "everything is a string" data model, which is
advantageous for configuration and text processing --- common
agentic AI tasks.

**Wasm-based sandboxes.**  WebAssembly runtimes (Wasmtime,
Wasmer) provide strong isolation through a compilation barrier.
TH8 occupies a different niche: it is an *interpreter* that
requires no compilation step, starts in microseconds, and can be
created and destroyed per-request with minimal overhead.

**Deno / Val Town.**  JavaScript runtimes with capability-based
security (Deno's `--allow-read`, `--allow-net`).  TH8's platform
abstraction provides a similar model at a lower level, without
the overhead of a JIT compiler or V8 runtime.


---

## 9.  Case Study: LadyBird Web Browser Integration

TH8 is being integrated into the LadyBird web browser as an
alternative scripting engine, handling `<script type="text/th8">`
elements alongside JavaScript.  This integration validates
several of TH8's core design decisions in a demanding
real-world context.

### 9.1  Architecture

The integration follows a multi-layer design:

-   **LibTH8** wraps the TH8 amalgamation in a C++ RAII class
    (`TH8::Interpreter`) using LadyBird's `AK::` type system
    (`ErrorOr`, `NonnullOwnPtr`, `StringView`).  The wrapper
    provides safe memory management, automatic cleanup, and
    idiomatic C++ access to the C API.

-   **WebPlatform** composes a security-hardened platform from
    four layers: null I/O (blocks all file and network access),
    libc (memory allocation), macOS zone allocator (when on
    Apple platforms), and POSIX (mutexes and time).  The result
    is an interpreter that can evaluate pure computation but
    cannot access the host filesystem, network, or process
    environment.

-   **DOM Bridge** maps TH8 commands to DOM API calls via a
    handle table (`GC::RootHashMap<u64, PlatformObject*>`) that
    integrates with LadyBird's garbage collector.  Scripts
    manipulate DOM objects through opaque handles rather than
    direct pointers, preventing type confusion and dangling
    references.  A maximum of 10,000 handles prevents resource
    exhaustion.

-   **DevTools** integration provides source-level debugging
    through the Firefox DevTools Protocol.  The `ThreadActor`
    class wires breakpoints, stepping, and frame inspection
    to TH8's debug API.  When a breakpoint fires, the debug
    callback returns `TH8_BREAK`, the NRE trampoline preserves
    the evaluation state on the heap, and control returns to
    LadyBird's event loop --- keeping the browser UI responsive
    while the script is paused.

### 9.2  Why TH8 Fits

Several TH8 design decisions proved directly valuable:

**Platform abstraction eliminates the sandbox.**  LadyBird does
not need to build or maintain a custom sandbox for TH8.  The
null-I/O platform layer makes dangerous operations structurally
impossible --- there are no file, network, or process commands
to hide, restrict, or audit.  This is a fundamental difference
from sandboxing JavaScript, where the engine has full OS access
and the sandbox must intercept and filter every syscall.

**Compile-time modularity reduces attack surface.**  LadyBird's
TH8 build disables `TH8_ENABLE_LOAD` (no binary loading),
`TH8_PLUGIN_IO` (no stdin/stdout), and
`TH8_PLUGIN_FILE_SYSTEMS` (no `source`, `cd`, `pwd`).  The
resulting interpreter has no commands that could even
theoretically escape the sandbox.

**Resource limits prevent denial of service.**  The default
configuration sets a 10-million-step limit and a 64 MB memory
ceiling.  Scripts that exceed these limits are terminated
gracefully without crashing the browser process.

**NRE-based debugging is event-loop-friendly.**  Traditional
debugger pause models block the calling thread (the browser's
main thread), making the UI unresponsive.  TH8's NRE trampoline
preserves the evaluation continuation on the heap, allowing
`Th8_Freeze` to return control to the event loop.  The browser
remains fully interactive while the script is paused.  This is
the same cooperative suspension model used for coroutines and
cancellation --- it was not designed for debugging, but it
turned out to be the ideal foundation for it.

### 9.3  Lessons

The LadyBird integration exposed two areas where the TH8 API
needed enhancement:

1.  **Per-callback platform context.**  The DOM bridge needed
    its own context pointer for the `xKeyValue` callback,
    separate from the POSIX platform's context.
    `Th8_SetPlatformContext` and `Th8_GetPlatformContext` were
    added to support per-callback context overrides via a
    hash table.

2.  **Runtime platform merge.**  Loading extensions via `[load]`
    required merging new callbacks into a running interpreter's
    platform.  `Th8_MergePlatformInterp` was added to clone the
    platform, merge, and replace --- with proper cleanup tracking
    in `Th8_DeleteInterp`.

Both additions were driven by the integration requirement and
are now part of the core API, available to all embedders.


---

## 10.  Conclusion

TH8 demonstrates that a full-fidelity Tcl implementation can be
built with security as a structural property rather than a
bolt-on feature.  The platform abstraction layer, composable
capability profiles, cryptographic script policy, compile-time
modularity, and resource limits provide a foundation for safe
embedding in scenarios ranging from configuration file evaluation
to agentic AI tool execution to web browser scripting.

The LadyBird integration validates this architecture in one of
the most demanding embedding contexts: a production web browser
processing untrusted content from the open internet.  The
platform abstraction eliminated the need for a custom sandbox,
the compile-time modularity removed unnecessary attack surface,
and the NRE-based debugging proved compatible with the browser's
event-loop architecture.

The Tcl Language Standard v1, together with its companion
specifications (TH8 Public C API Specification, Tcl Language
Extensions), carries 1,151 normative requirements with 98.52%
R-marker test coverage and 2,511 conformance test citations.
The current draft is a **working text**, not a submission-ready
formal standard: it has the specification discipline needed to
give embedders confidence that the interpreter behaves as
documented, but the structural and editorial polish required
for a formal standardization body (ISO, ECMA, IEEE) --- a
consistent conformance-class model, exhaustive edge-case
specification of every command, and standards-house typographic
conventions --- is deferred work.

The development methodology --- sustained human-LLM collaboration
with clear role separation --- proved effective for this class of
project: formally specified, security-critical, with a large
surface area of tests and documentation.  The LLM was not a
replacement for the human architect but a force multiplier that
made a solo developer's ambition feasible.

For the Tcl community, TH8 offers a proof of concept that the
language's simplicity and flexibility --- the qualities that
made it the "tool command language" --- are exactly the qualities
that make it suitable for the next generation of software
systems, where the programmer may be a human, an AI, or both.


---

## Acknowledgments

The author thanks the Tcl community for decades of language design
wisdom; the SQLite project for pioneering the requirement-marking
methodology used in the TH8 standard; Anthropic's Claude for
tireless collaboration across hundreds of development sessions;
and the memory of Miguel Sofer, whose Non-Recursive Engine made
TH8's coroutine and suspension architecture possible.


---

## References

1.  J. Ousterhout, "Tcl: An Embeddable Command Language," in
    *Proc. USENIX Winter Technical Conference*, 1990.

2.  D. R. Hipp, "Fossil: Simple, High-Reliability, Distributed
    Software Configuration Management," 2007.
    https://fossil-scm.org/

3.  M. Sofer, "NRE --- Non-Recursive Engine for Tcl," Tcl
    Improvement Proposal #327, 2009.

4.  "SQLite Requirements," https://www.sqlite.org/requirements.html

5.  J. Auriemma and P. Vixie, "The Security of Scripting
    Language Interpreters," *;login:*, USENIX, 2018.

6.  Anthropic, "Claude Code: An Agentic Coding Tool,"
    https://docs.anthropic.com/en/docs/agents-and-tools, 2025.
