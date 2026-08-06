# TH8 Agent Surface Area Analysis

This document analyses TH8's script-level capabilities for agentic
AI use cases and identifies the gaps that remain.  Each category
carries a 0-10 readiness score derived from a documented capability
inventory; the trailing roadmap groups outstanding features by
implementation cost and security implication.

The companion document `agent_memory.md` describes why TH8's
syntax is a strong fit for agent **memory representation**; this
document is concerned with what TH8 can **do** once memories are
loaded.  Both are working documents -- the underlying TH8 release
is stable, but the agentic-use case is evolving and these
analyses will be refreshed as new capabilities land.

*Last currency review: 2026-07-03 (cross-checked against the
current TH8 command surface; includes the `[binary]` ensemble
with its `j`/`J` bigint specifiers).  Live release status,
per-iteration changes, and current test/coverage numbers live
in the project's [`RELEASE_NOTES.md`](RELEASE_NOTES.md).*

---

## 1.  Current Capabilities by Category

### 1.1  Data Processing (8/10)

**Strong:**
- 20 dict subcommands (create, get, set, exists, filter, for,
  map, merge, remove, replace, keys, values, etc.)
- 13 list commands (lappend, lassign, lindex, list, llength,
  lrange, lremove, lreplace, lreverse, lsearch, lsort, join,
  split)
- PostgreSQL Spencer regex engine (regexp, regsub)
- Pattern matching via string match (glob) and lsearch

**Gap:** No native JSON/XML/CSV parsers.  JSON is available via
the SQLite extension's `json` command (see Section 3).  CSV and
XML require regex-based manual parsing.

### 1.2  String Manipulation (9/10)

**Strong:**
- 22 string subcommands (compare, equal, first, index, is, last,
  length, map, match, range, repeat, replace, tolower, totitle,
  toupper, trim, trimleft, trimright, wordend, wordstart,
  bytelength, reverse)
- Case-insensitive variants (-nocase flag) on compare, equal,
  match, map
- concat command, append command, format/scan

**Gap:** No `string cat` (Tcl 8.7+; workaround: `append`).
No encoding conversion beyond UTF-8.  Limited `scan` (no
suppression flag, no list-mode return).

### 1.3  Math and Logic (10/10)

**Complete:**
- 60+ math functions (trig, hyperbolic, exponential, logarithmic,
  special, classification, rounding, IEEE 754)
- Boolean, bitwise, ternary operators
- Arbitrary-precision integers (libtommath)
- Float classification (isfinite, isinf, isnan, isnormal, etc.)

### 1.4  I/O and Persistence (4/10)

**Available:**
- Channel I/O: puts, gets, read, close, flush, seek, tell
- Standard channels: stdin, stdout, stderr
- Temporary file channels (file tempname)
- Key-value store (kv command with env or SQLite backend)
- Source/load for scripts and extensions

**Gap:** No file write/create/delete/rename/mkdir (read-only by
design).  No `open` with mode specifications.  No socket.
These are intentional security boundaries --- I/O is mediated
by the platform callback layer.

### 1.5  Control Flow (9/10)

**Strong:**
- if/elseif/else, switch (glob/exact/regexp), for, foreach, while
- break, continue, return with -code/-level options
- catch with resultVar, try/finally
- eval, subst with flag control
- Coroutines (coroutine/yield), tail calls
- Asynchronous cancellation (interp cancel)

**Gap:** No `lmap` (Tcl 8.6+; workaround: foreach + lappend).

### 1.6  Introspection (7/10)

**Strong:**
- info commands, info procs, info exists, info body, info args,
  info default, info vars, info globals, info level
- info script, info nameofexecutable, info patchlevel
- info loaded, info plugins, info subcommands, info functions

**Gap:** No `info hostname`.  No stack trace inspection beyond
errorInfo string.  No memory usage introspection.

### 1.7  Network (1/10)

**Available:**
- clock https (HTTPS time sync with signature verification)
- clock ntp (NTP v4 multi-server consensus)
- libcurl infrastructure at C level (not exposed to scripts)

**Gap:** No http command.  No socket.  No URL parsing.
This is the largest gap for autonomous agents.

### 1.8  Encoding and binary (8/10)

**Available:**
- base64 encode/decode (production quality)
- hash (SHA-512)
- UTF-8 strict validation built into the parser
- `binary format` / `binary scan` (full Tcl 8.6 surface: integer,
  float, string, bit, hex, cursor specifiers) **plus the TH8 `j`
  and `J` extension specifiers for round-trip bit-import/export
  of arbitrary-precision bigints**
- Hex encode/decode at the script level via `binary format H*` /
  `binary scan H*`

**Gap:** No `encoding` command for character-set conversion
(TH8 is UTF-8-strict throughout, by design).

### 1.9  Security (9/10)

**Strong:**
- RSA-SHA512 script signing (harpy sign/verify)
- Signed-only policy enforcement
- Resource limits (step, alloc, result size, depth)
- Zero-capability default platform
- Compile-time command removal

---

## 2.  AI-Agent-Readiness Scorecard

| Category | Score | Notes |
|----------|-------|-------|
| Data processing | 8/10 | JSON via SQLite; no CSV/XML native |
| String/text | 9/10 | Comprehensive; no encoding conversion |
| Math/logic | 10/10 | Complete; bigint available |
| I/O/persistence | 4/10 | Read-only files; kv store helps |
| Control flow | 9/10 | No lmap; coroutines excellent |
| Introspection | 7/10 | Good but limited process/memory info |
| Network | 1/10 | No HTTP, socket, or protocol support |
| Encoding / binary | 8/10 | Base64, SHA-512, full `binary format`/`scan` incl. bigint `j`/`J` |
| Security | 9/10 | Excellent sandbox model |

**Overall: 6.5/10 for autonomous agents, 8.5/10 for
orchestrated agents (host-driven callbacks).**

---

## 3.  Prioritized Improvement Roadmap

### Phase 1: Pure Computation (No Security Risk)

These additions involve no I/O, no system interaction, and no
security implications.  They are safe to add to the core
interpreter.

| Feature | Impact | Complexity | Notes |
|---------|--------|------------|-------|
| `json` command (via SQLite) | HIGH | Medium | Leverages SQLite's built-in JSON functions; 16 subcommands |
| `lmap` | MEDIUM | Low | Syntactic sugar over foreach + lappend |
| `string cat` | LOW | Trivial | Concatenation without variable; Tcl 8.7+ compat |
| ~~Hex encode/decode~~ | MEDIUM | -- | **Shipped** via `binary format H*` / `binary scan H*` |

### Phase 2: Platform-Mediated (Callback-Controlled)

These additions require platform callbacks and are controlled
by the embedder.  They are safe when the appropriate callbacks
are provided.

| Feature | Impact | Complexity | Notes |
|---------|--------|------------|-------|
| HTTP client | HIGH | High | Wrap libcurl platform; script-level get/post |
| File write (`xPutData`) | HIGH | Medium | New platform callback for write access |
| Env var script access | MEDIUM | Low | Script wrapper around kv with env backend |
| `glob` command | MEDIUM | Medium | File listing via platform callback |

### Phase 3: Advanced

These require significant design work and may have security
implications that need careful analysis.

| Feature | Impact | Complexity | Notes |
|---------|--------|------------|-------|
| `encoding` command | MEDIUM | High | Multi-charset via ConvertUTF_v2 extensions |
| ~~`binary` command~~ | MEDIUM | -- | **Shipped** in 1.0.0; full Tcl 8.6 surface plus TH8 `j`/`J` bigint specifiers |
| Async I/O | MEDIUM | Very High | Event loop, fileevent equivalent |
| `socket` command | HIGH | Very High | TCP/UDP via platform callbacks |

---

## 4.  Recommendations for Agentic Deployments

### Use TH8 for agents when:

- Agent logic is primarily computation, data transformation,
  and control flow
- I/O is managed by the host application (agents call back into
  the host for HTTP, file operations, etc.)
- Sandboxing and security are paramount
- Agent state can be persisted via the key-value store
- JSON processing is available via the SQLite extension

### Consider alternatives when:

- Agents need autonomous HTTP client capabilities without host
  mediation
- Direct filesystem access (logging, checkpointing) is required
- Multi-charset or binary protocol support is essential
- Subprocess execution is needed

### Hybrid approach (recommended):

The most effective deployment uses TH8 as the **computation
and policy layer** with the host providing **I/O capabilities**
via platform callbacks:

```
Agent Script (TH8)
    |
    +-- Pure computation (math, string, dict, list, regex)
    +-- JSON processing (via SQLite extension)
    +-- State persistence (via kv command + SQLite backend)
    +-- Policy enforcement (signed-only, resource limits)
    |
Host Application
    |
    +-- HTTP client (callback-mediated)
    +-- File I/O (callback-mediated)
    +-- Process management
    +-- Network access
```

---

## 5.  Tcl 8.6+ Features Not in TH8

| Feature | Tcl 8.6 | TH8 | Workaround |
|---------|---------|-----|------------|
| `lmap` | Yes | Planned (Phase 1) | foreach + lappend |
| `string cat` | Yes (8.7) | Planned (Phase 1) | append command |
| `encoding` | Yes | Planned (Phase 3) | UTF-8 only |
| `binary` | Yes | **Shipped in 1.0.0** | Full Tcl 8.6 surface + `j`/`J` bigint extension |
| `socket` | Yes | Planned (Phase 3) | Host-provided callback |
| `http` | Yes (tcllib) | Planned (Phase 2) | Host-provided callback |
| `exec` | Yes | Intentionally absent | Security boundary |
| `open` (write) | Yes | Intentionally absent | Security boundary |
| `chan` | Yes | Partial (stdin/stdout/temp) | Platform callbacks |
| `info hostname` | Yes | Missing | Host-provided at init |
