# TH8 C Source Code Style

This document is the canonical specification for the C coding
style used across the TH8 source tree.  It merges:

* **TIP 247** -- the Tcl/Tk Engineering Manual
  (https://core.tcl-lang.org/tips/doc/trunk/tip/247.md), which
  defines the historical Tcl C style.
* **TH8 conventions** -- adaptations and additions inspired by
  the SQLite source-code style (identifier casing, header
  comments, naming discipline).  Any deviation from TIP 247 is
  called out explicitly below.

The intent is one place to look up the rules and one tool chain
to enforce them.  Where TH8 deviates from TIP 247 the deviation
is called out explicitly.

## How rules are enforced

Every mechanically-checkable rule in this document points at the
tool that enforces it:

* **`.clang-format`** -- repo-root config; structural rules
  (indent, brace placement, line length, continuation, pointer
  alignment, etc.).  Run via `tools/format_code.tcl --check`
  or `tools/format_code.tcl --write` for in-place fixes.
* **`tools/audit_patterns.tcl`** -- Tcl-driven regex/scan rules
  that clang-format cannot express (project-specific naming,
  banned CRT calls in source, the `AUDIT-OK` suppression
  marker, header-guard format, the no-non-ASCII rule, and the
  post-link CRT object scan).  Each rule below cites its
  `audit_rule` id when applicable.
* **Manual review** -- taste-based rules (comment quality,
  procedure-header completeness) that resist mechanical
  enforcement.  Reviewers are expected to enforce them.

`make audit` is the entry point that runs the source-rule scan
(`audit_patterns.tcl source`) and the post-link CRT object scan
(`audit_patterns.tcl crt-objects $(B)`); it is a hard gate on
`make all` on both POSIX and MSVC.

`make audit-format` runs the clang-format `--check` pass.  This
target is held outside the `all` chain until the existing source
is brought into clang-format compliance; once clean, it folds
back into `audit` and into the hard gate.

## Enforcement roadmap

A subset of the rules below cite an `audit_patterns.tcl#<rule-id>`
tag marked *(planned)* rather than a live rule.  These rules
are documented as normative style, but the mechanical checker
for them has not yet shipped.  Reviewers are expected to enforce
them at code-review time until the checker catches up.

Currently *(planned)* rule ids -- to be moved into
`audit_patterns.tcl` in a future release:

* `public-fn-prefix` -- verifies public functions start with
  `Th8_` followed by PascalCase.
* `internal-fn-prefix` -- verifies internal `TH8_INTERNAL`
  functions start with `th8` followed by camelCase.
* `define-all-caps` -- verifies `#define` names are ALL_CAPS.
* `static-for-file-scope-decls` -- verifies file-scope
  declarations without a package prefix are marked `static`.
* `header-guard-format` -- verifies each header's include-guard
  macro matches `_FILENAME_H`.
* `extern-in-source` -- verifies `extern` declarations do not
  appear in `.c` files (they belong in headers).
* `crt-source-call` -- verifies banned CRT calls in source
  (`malloc`, `open`, `printf`, ...) are routed through the
  platform layer.
* `macro-args-parenthesized` -- verifies function-like macros
  parenthesize each argument reference.

The `no-non-ASCII` rule and the `AUDIT-OK[rule-id]` suppression
marker are already active in `audit_patterns.tcl` today.

## Severity legend

* **MUST** -- non-compliant code is a build failure (or treated
  as one in code review).
* **SHOULD** -- non-compliant code is permitted only when a
  clearly-justified `AUDIT-OK[rule-id]: reason` marker accompanies
  the deviation.
* **MAY** -- guidance; deviation requires no marker.

---

## 1.  Naming

### 1.1  Variables

Variable names start with a lower-case letter.  Multi-word names
use camelCase; underscores are not used as word separators.
**MUST.** *(TIP 247 Sec."Procedure and Variable Names".)*

TH8 follows SQLite convention for short standard names:

* `int rc` -- return code
* `const char *z` -- string pointer (single-character names use
  `z` as the prefix when more than one of a kind exists, e.g.
  `zPath`, `zName`)
* `int n` -- count
* `size_t nByte`, `size_t nLen` -- size in bytes / length

**Checked by**: `audit_patterns.tcl#internal-uppercase-name`
catches a sub-case (internal symbols starting with uppercase).
Full-coverage variable casing remains manual review.

### 1.2  Pointers

TH8 uses the **`p` prefix** convention (`pInterp`, `pCmd`, `pPlat`)
rather than the TIP 247 `Ptr` suffix.  Pointer-to-pointer is
`pp` (`ppEntry`).  This is a deliberate deviation from
TIP 247 Sec."Procedure and Variable Names".  **MUST** for new code;
existing code that uses other conventions is grandfathered.

### 1.3  Public API functions and types

Names start with `Th8_` followed by PascalCase
(`Th8_CreateInterp`, `Th8_GetVar`, `Th8_Interp`).
**MUST.** *(SQLite/Tcl convention; TH8-specific.)*

**Checked by**: `audit_patterns.tcl#public-fn-prefix` (planned).

### 1.4  Internal functions

Names start with `th8` followed by camelCase
(`th8FreeVariable`, `th8NextWord`, `th8MallocCommon`).
File-static helpers may omit the prefix and use `xxx_command`
or `xxx_step` (matches the Tcl command name being implemented).
**MUST.**

**Checked by**: `audit_patterns.tcl#internal-fn-prefix` (planned).

### 1.5  Macros and `#define` constants

Macro and `#define`d constant names are ALL_CAPS with underscore
word separators (`TH8_OK`, `TH8_MX_ALLOC`, `TH8_NOLEN`).
Function-like macros that replace procedures may use the same
casing as the procedure they replace.  **MUST.**
*(TIP 247 Sec."Procedure and Variable Names".)*

**Checked by**: clang-format enforces alignment via
`AlignConsecutiveMacros: Consecutive`; casing is checked by
`audit_patterns.tcl#define-all-caps` (planned).

### 1.6  Module prefix

Symbols exported from a package have the package prefix
followed by an underscore (`Th8_`, `Tcl_`).  Symbols used across
files within a package but not exported have the package prefix
without the underscore (`th8`).  Symbols local to a single file
need no prefix but **MUST** be declared `static`.
*(TIP 247 Sec."Procedure and Variable Names".)*

**Checked by**: `audit_patterns.tcl#static-for-file-scope-decls`
(planned, partial).

---

## 2.  Brace placement and indentation

### 2.1  Indentation

Each level of indent is **four spaces**.  Tabs are permitted only
to represent every-other indent level when the column would
otherwise be 8: TH8 uses `IndentWidth: 4`, `TabWidth: 8`,
`UseTab: ForIndentation`, so the first indent level is four
spaces, the second is one tab, the third is one tab plus four
spaces, and so on.  See `.clang-format` at repo root.
**MUST.** *(TIP 247 Sec."Indentation"; TH8 deviation: tabs at
column 8 instead of always-spaces.)*

### 2.2  Continuation lines

Continuation lines are indented one continuation step (4 spaces
by clang-format default).  Continuation lines should start with
an operator (`*`, `&&`, `||`) so the structure is visible at a
glance.  **SHOULD.** *(TIP 247 Sec."Indentation".)*

### 2.3  Line length

Lines are kept under **78 columns** including tab expansion.
clang-format `ColumnLimit: 78`.  **MUST.**
*(TIP 247 Sec."Indentation" sets the limit at 80; TH8 tightens
slightly to leave margin for diff annotation.)*

### 2.4  Brace style

Function-definition opening brace on its own line (Allman style).
Control-statement (`if`, `for`, `while`, `switch`, `do`) opening
brace at end of preceding line (K&R style).  Closing brace on
its own line at the same indent as the construct that opened it.
**MUST.** *(TIP 247 Sec."Brace placement"; clang-format
`BreakBeforeBraces: Linux`.)*

`else` is cuddled with both the closing brace of the `if` body
and the opening brace of the `else` body: `} else {`.
**MUST.**

### 2.5  Always brace compound statements

Single-statement bodies of `if`, `for`, `while`, etc.
**MUST** be braced -- except for the single idiom
`if (cond) return rc;` (no `else` branch), which is permitted
because it is pervasive in existing TH8 code.  clang-format
`AllowShortIfStatementsOnASingleLine: WithoutElse`.

### 2.6  Comments

Block comments documenting code occupy full lines:
`/*` on a line by itself, comment text indented inside,
`*/` on its own line.  Trailing comments on the same line as
code are reserved for declarations and array members.  **MUST.**
*(TIP 247 Sec."Comments".)*

clang-format does NOT reflow comments (`ReflowComments: Never`)
and does NOT realign trailing-comment columns
(`AlignTrailingComments.Kind: Leave`).  Hand-tuned comment
layout is preserved verbatim.

---

## 3.  File organization

### 3.1  Header file structure

Headers begin with the file-level comment block (purpose,
copyright), followed by the multiple-include guard
(`#ifndef _FILENAME_H` / `#define _FILENAME_H` / `#endif`),
followed by `#include` directives, then declarations.
**MUST.** *(TIP 247 Sec."Header file structure".)*

**Checked by**: `audit_patterns.tcl#header-guard-format`
(planned).

### 3.2  Source file structure

`.c` files begin with the file-level comment block, then
`#include` directives, then declarations.  **Source files MUST
NOT contain `extern` statements** -- declarations live in headers.
*(TIP 247 Sec."Source file structure".)*

**Checked by**: `audit_patterns.tcl#extern-in-source` (planned).

### 3.3  Meta-header inclusion order

TH8 organises its includes in a fixed layered order to enforce
platform-layer discipline (libc, msvc, win32, posix, macos).
Meta-headers do not include each other -- `.c` files list them
explicitly in logical order.  **MUST.**

**Checked by**: `audit_patterns.tcl#meta-header-cross-include`.

### 3.4  Platform-layer discipline

Each platform file uses APIs only from its own abstraction layer.
POSIX files use POSIX, Win32 files use Win32, etc.
**MUST.**

Cross-layer leakage is enforced by code review and by the
banned-function list (CRT calls in non-platform code).

---

## 4.  Procedure documentation

### 4.1  Header comment

Every public function has a multi-line header comment of the
form:

```c
/*
 *----------------------------------------------------------------------
 *
 * Th8_FunctionName --
 *
 *      Single-paragraph abstract describing what the function
 *      does (not how).
 *
 * Why / How:
 *      Optional section explaining design rationale or
 *      noteworthy mechanism.
 *
 * Results:
 *      Return-value semantics (TH8_OK / TH8_ERROR, NULL, etc.).
 *
 * Side effects:
 *      Anything observable to the caller beyond the return
 *      value (interp result, allocation, callbacks, etc.).
 *
 *----------------------------------------------------------------------
 */
```

**MUST** for all public (`Th8_*`) functions and any internal
helper of non-trivial complexity.  **SHOULD** for trivial
file-local helpers.  *(TIP 247 Sec."Procedure header"; TH8
TH8 style requires per-function
headers -- no group-style shared headers.)*

### 4.2  Argument documentation

Each parameter is declared one per line with a trailing comment
describing the parameter's role.  Long parameter lists wrap one
per line (clang-format `BinPackParameters: false`).
**MUST.** *(TIP 247 Sec."Procedure header".)*

### 4.3  Return type on its own line

Function definitions place the return type on its own line and
the function name + parameters on the next line.
**MUST.** *(TIP 247 Sec."Procedure header"; clang-format
`AlwaysBreakAfterReturnType: AllDefinitions`.)*

---

## 5.  Memory and resource management

### 5.1  Allocation

Internal code calls `Th8_AttemptMalloc` (returns NULL on
failure) or `Th8_SafeAlloc` family.  Direct calls to `malloc`,
`calloc`, `realloc`, `free` are banned outside the platform
allocators.  **MUST.** *(TH8 conventions; analogous to
TIP 247 Sec."Memory allocation".)*

**Checked by**: `audit_patterns.tcl#direct-libc-malloc`,
`#direct-libc-calloc`, `#direct-libc-realloc`,
`#bare-th8-alloc-primitive`.

### 5.2  Banned CRT functions

The full banned-function list lives in
`tools/data/crt_functions.txt`.  Source-level calls are flagged
by `audit_patterns.tcl#crt-source-call` (planned); object-level
references are flagged by `audit_patterns.tcl crt-objects`
(planned, replaces the previous `tools/check_crt.sh` /
`check_crt_msvc.bat`).  Allow-list (per-file exemptions) lives
in `tools/data/crt_exceptions.txt`.

### 5.3  Overflow-safe size arithmetic

Bare `a * b` on `size_t` operands is banned.  Use
`TH8_SAFE_MUL_SIZE` / `TH8_ALLOC_MUL_ADD` /
`TH8_ALLOC_STR_*` macros instead.  **MUST.**

**Checked by**: `audit_patterns.tcl#size-cast-multiply-after`,
`#size-cast-multiply-before`, `#size-name-multiply`,
`#libc-alloc-with-multiply`.

### 5.4  Return value checking

Every function return value **MUST** be checked for failure,
even when "impossible".

Detection is structural and not currently codified as a
mechanical check -- manual review.

### 5.5  Pointer NULL convention

Pointer-returning functions return `NULL` on failure, never
bare `0`.  **MUST.**

Manual review (regex `return\s+0\s*;` would catch many
unrelated cases).

---

## 6.  Macros

### 6.1  Argument parenthesization

When defining function-like macros, every parameter use is
wrapped in parentheses, and the entire expansion is wrapped:

```c
#define TH8_MAX(a, b) (((a) > (b)) ? (a) : (b))
```

**MUST.** *(TIP 247 Sec."Macros".)*

**Checked by**: `audit_patterns.tcl#macro-args-parenthesized`
(planned, partial).

### 6.2  Avoid complex macros

Procedures are preferred over macros for non-trivial logic.
Macros are reserved for cases where the inline-ability is
performance-critical or where the macro is a thin wrapper that
captures `__FILE__` / `__LINE__`.  **SHOULD.**
*(TIP 247 Sec."Macros".)*

---

## 7.  ASCII-only source

All TH8 source files (`.c`, `.h`, `.tcl` test files, doc
markdown) are 7-bit ASCII.  Any byte > 0x7F is a violation.
This applies to comments, string literals, identifiers, and
whitespace.  Use `\xNN` or `\uNNNN` escapes inside string
literals where non-ASCII bytes are needed.
**MUST.** *(TH8-specific; not in TIP 247.)*

**Checked by**: `audit_patterns.tcl#no-non-ascii` (active,
hard-gated in `make all`).

---

## 8.  Returns codes

### 8.1  Boolean

Boolean yes/no return values use plain `int` (`1` or `0`).
**MUST.**

### 8.2  Success / failure

Functions that report success or failure return one of the
named constants `TH8_OK`, `TH8_ERROR`, etc.  Never use bare
integers (`return 1` for "error") for these channels.
**MUST.**

### 8.3  Multi-state return values

Three-or-more-state returns get named `#define` constants
(`TH8_WAIT_OK`, `TH8_WAIT_RETRY`, `TH8_WAIT_ERROR`,
`TH8_WAIT_TIMEOUT`), never bare integers.  **MUST.**

---

## 9.  Defensive macros: `NEVER` / `ALWAYS`

`NEVER(X)` and `ALWAYS(X)` mark conditions believed unreachable
in production:

* **Production builds** -- both expand to `(X)` (transparent).
* **`TH8_OMIT_AUXILIARY_SAFETY_CHECKS`** (coverage build) --
  `NEVER(X) -> 0`, `ALWAYS(X) -> 1`.  llvm-cov reports the
  affected MC/DC pairs as `constant folded` and excludes them
  from the denominator, so coverage measurement reflects only
  conditions the developer believes observable.
* **`TH8_DEBUG`** -- `NEVER(X) -> ((X) ? (assert(0), 1) : 0)`,
  `ALWAYS(X) -> ((X) ? 1 : (assert(0), 0))`.  An execution that
  violates the wrap aborts at the offending site.

Use these wraps to mark genuinely unreachable defensive
guards.  See `src/th8.h` lines 234-242 for the canonical
expansion.  See also the conference paper Sec.4.13 for the
fuzz-testing synergy.  **MUST** for new code; legacy guards
should be wrapped opportunistically as they are reviewed.

---

## 10.  Suppression marker

Source-level rules support an inline marker that disables a
specific rule on a specific line range:

```c
/* AUDIT-OK[rule-name]: short justification */
some_apparently_violating_code();
```

The rule name in brackets is mandatory.  The marker applies to
the same line if placed at the end of a line, or to the next
line of code if placed on its own line.  Markers without a
justification are themselves flagged.

Object-level CRT exceptions are NOT controlled by this marker;
they live in `tools/data/crt_exceptions.txt` instead.

---

## Appendix A: Banned CRT functions (source and object scope)

The list of banned C runtime functions lives in
`tools/data/crt_functions.txt` (one per line, comments allowed).
The list is shared between the source-level scan
(`audit_patterns.tcl#crt-source-call`) and the object-level
scan (`audit_patterns.tcl crt-objects`).

Categories:

* **Memory** -- `malloc`, `calloc`, `realloc`, `free`,
  `strdup`, `strndup`, `aligned_alloc`, `posix_memalign`.
* **Unsafe string** -- `strcpy`, `strcat`, `gets`, `sprintf`,
  `vsprintf`, `scanf`, `vscanf`.
* **Bounded but discouraged** -- `strncpy`, `strncat`
  (truncation footgun; use `Th8_*` equivalents).
* **Process / signal** -- `abort`, `exit`, `_exit`, `raise`,
  `kill`, `signal`, `system`.
* **Filesystem** -- `chdir`, `getcwd`, `unlink`, `rename`,
  `mkdir`, `rmdir`, `tempnam`, `tmpnam`, `mktemp`.
* **Stdio** -- `printf`, `fprintf`, `fopen`, `fclose`, `fread`,
  `fwrite`, `fseek`, `ftell`, `fgets`, `fputs`.
* **Time** -- `time`, `gettimeofday`, `clock`, `localtime`,
  `gmtime`, `strftime`.

The authoritative list is the data file; this appendix is for
orientation only.

## Appendix B: Allow-list and per-file exceptions

Object-level allow-list (whole-file exemption from the CRT
scan) covers the small set of files that legitimately wrap or
implement the CRT interface for the rest of the project:

* `th8_libc` -- TH8's libc-shim platform layer
* `th8_cosmopolitan` -- cosmopolitan libc backend
* `bestline` -- vendored line-editor library
* `mimalloc_static` -- vendored allocator
* `th8sh` -- the shell front-end (uses libc directly)

Compiler-generated calls allowed everywhere (no need to
explicitly mark them):

* `memcpy`, `memset`, `memmove`, `memcmp`

Per-file symbol exceptions live in
`tools/data/crt_exceptions.txt` in the form:

```
allow th8_posix abort raise getenv unlink
```

## Appendix C: Tooling cross-reference

| Concern | Tool | Invocation |
|---|---|---|
| Formatting (indent, braces, line length, etc.) | clang-format | `tools/format_code.tcl --check` |
| Source-level rule scan | audit_patterns.tcl | `tools/audit_patterns.tcl source` |
| Object-level CRT scan | audit_patterns.tcl | `tools/audit_patterns.tcl crt-objects bin/` |
| Combined audit (build gate) | audit_patterns.tcl | `tools/audit_patterns.tcl all bin/` |
| Make integration | Makefile / Makefile.msc | `make audit` (also runs as part of `all`) |

`make audit` is wired as a dependency of `all` on both the
POSIX and MSVC build, matching how `manlint` is wired today.
A failing audit fails the build.
