# CLAUDE.md -- AI Agent Guide for the TH8 Codebase

This document is for AI agents (Claude Code, etc.) working with the TH8
interpreter.  It covers building, testing, coding conventions, and the
full command reference.

---

## 1. Project Overview

TH8 is a **secure, embeddable Tcl-compatible scripting language**.
It is the combined evolution of three ancestors:

- **TH1** (Fossil's scripting engine) --- proved that a minimal Tcl
  interpreter can safely process untrusted input in an embedded
  context.  15 years in Fossil with zero security incidents.  TH8
  inherits TH1's minimalism: no bytecode, no threading model,
  single-file embeddability.
- **Tcl 8.x** --- provides the language definition and surface
  contract.  TH8's formal specification is written against Tcl 8.4
  semantics as the correctness baseline.  The command set,
  substitution rules, "everything is a string" data model, and
  control flow are all Tcl.
- **Eagle** --- contributes the security thinking, production
  discipline, and specific technologies: Spilornis list parser,
  Harpy signing concept, `nproc`/`napply`/`downlevel`, ConvertUTF_v2,
  structured return codes, and the cancellation model.  Eagle's 18
  years of running untrusted code shaped the philosophy:
  zero-capability default, embedder-controlled boundaries.

TH8 lives within the SQLite ecosystem and targets ISO submission and
a Tcl Conference 2026 presentation.

Key facts:

- **Version:** 1.0.0
- **Language surface:** Tcl 8.4 syntax with selected extensions
  (non-recursive engine, `**` exponent operator, `nproc`/`napply`,
  strict UTF-8)
- **Formal standard:** `doc/tcl_language_standard_v1.md` with 854+
  R-marker requirements
- **Correctness oracle:** Tcl 8.4 behavior is canonical; Eagle behavior
  is canonical for TH8-specific extensions
- **License:** see `license.terms`

---

## 2. Building

### Unix (make)

```sh
make fresh ENABLE_TEST_KEY=1
```

**Always use `make fresh`** as the first build target.  It runs `clean`
then `all`, which includes `genstubs` (stubs table regeneration).  This
avoids stale object files and out-of-date generated headers.

### MSVC (nmake)

```bat
nmake /f Makefile.msc ENABLE_TEST_KEY=1
```

**Always update `Makefile.msc` alongside `Makefile`** when changing build
rules.

### Build Toggles

| Make variable            | C define                      | Default | Purpose                                      |
|--------------------------|-------------------------------|---------|----------------------------------------------|
| `ENABLE_TEST_KEY=1`      | `TH8_ENABLE_TEST_KEY`         | 0       | Include test signing key (needed for tests)   |
| `ENABLE_FAULT_INJECTION` | `TH8_ENABLE_FAULT_INJECTION`  | 1       | Fault injection platform layer                |
| `ENABLE_BESTLINE`        | `TH8_USE_BESTLINE`            | 1       | bestline readline replacement (Unix only)     |
| `ENABLE_REGEXP`          | `TH8_ENABLE_REGEXP`           | 1       | PostgreSQL Spencer regex engine               |
| `ENABLE_BIGINT`          | `TH8_ENABLE_BIGINT`           | 1       | Arbitrary-precision integers (libtommath)     |
| `ENABLE_CRYPTOGRAPHY`    | `TH8_ENABLE_CRYPTOGRAPHY`     | 1       | RSA signing, harpy certificates, key mgmt     |
| `ENABLE_UNBOUND`         | `TH8_ENABLE_UNBOUND`          | 1       | DNSSEC-validated DNS (libunbound)             |
| `ENABLE_LIBCURL`         | `TH8_ENABLE_LIBCURL`          | 1       | libcurl-backed NTP and HTTPS                  |
| `ENABLE_MIMALLOC`        | `TH8_USE_MIMALLOC`            | 1       | Microsoft mimalloc allocator                  |
| `ENABLE_EXPRESSIONS`     | `TH8_ENABLE_EXPRESSIONS`      | 1       | Expression engine (disabling cascades)        |
| `ENABLE_VARIABLES`       | `TH8_ENABLE_VARIABLES`        | 1       | Variable system                               |
| `ENABLE_LOAD`            | `TH8_ENABLE_LOAD`             | 1       | Dynamic library loading                       |

### Build Artifacts

| Artifact                 | Location        | Description                       |
|--------------------------|-----------------|-----------------------------------|
| `th8sh`                  | `bin/`          | Interactive shell                 |
| `libth8.a`               | `bin/`          | Static library                    |
| `libth8.so` / `.dylib`   | `bin/`          | Shared library                    |
| `libth8stub.a`           | `bin/`          | Stubs library for extensions      |
| `libth8test.so`          | `bin/`          | Test library (C test commands)    |
| `libth8bridge_tcl.so`    | `bin/`          | Tcl-to-TH8 bridge                |
| `libth8bridge_th8.so`    | `bin/`          | TH8-to-Tcl bridge                |
| `th8shs`                 | `bin-static/`   | Statically linked shell           |

---

## 3. Running Tests

```sh
TH8SH_YES_TESTLIB=1 ./bin/th8sh tests/all.tcl
```

### Hard Rules

- **All tests must pass (0 failures)** before any change is considered
  complete.
- **Never modify test expected results** -- fix the TH8 C code instead.
- Do not modify the user's script library or test infrastructure files
  (`lib/Standard1.0/*`, `tests/prologue.tcl`, `tests/epilogue.tcl`,
  `tests/all.tcl` package-handling code) without asking.
- Test support routines go in `lib/Standard1.0/test.tcl`, not in the
  individual test files.
- New test files may be added to `tests/all.tcl` alphabetically; do not
  touch the package handling code in that file.

### R-Markers

Every test references an R-marker from the formal standard.  R-markers
must be **MD5-derived via `tools/mkreq.tcl`**, never randomly invented.
When editing requirement text in the standard, recompute the R-marker and
update all test references.

### Cross-Engine Verification

Tests should be verifiable in three engines:
1. **TH8** (primary target)
2. **Tcl 8.4+** (correctness oracle)
3. **Eagle** (correctness oracle for TH8-specific extensions)

### Re-signing Modified Scripts

All `.tcl` test files have corresponding `.b64sig` signature files.
After modifying a script, re-sign it:

```sh
bash tools/signScript.sh <file>
```

---

## 4. Complete Command Reference

### Binary I/O

The `binary` ensemble (gate: `TH8_PLUGIN_BINARY`) packs and unpacks
arbitrary byte sequences via a format-string mini-language.  TH8
implements the full Tcl 8.6 surface plus one extension (`j` / `J`
for arbitrary-precision bigint round-trip).

| Command         | Synopsis                                                   |
|-----------------|------------------------------------------------------------|
| `binary format` | `binary format formatString ?arg ...?` -- pack values into a byte string |
| `binary scan`   | `binary scan binaryData formatString ?varName ...?` -- unpack byte string into named variables; returns the number of variables successfully assigned |

#### Format specifier letters

A *formatString* is a sequence of *letter* `?count?` tokens
separated by optional whitespace.  Each letter consumes (on
`format`) or produces (on `scan`) one or more bytes per its
width column below.

**Integer specifiers** (signed on `binary scan`, two's-complement
truncation on `binary format`):

| Letter | Width   | Byte order      |
|--------|---------|-----------------|
| `c`    | 8 bits  | native (n/a)    |
| `s`    | 16 bits | little-endian   |
| `S`    | 16 bits | big-endian      |
| `t`    | 16 bits | host native     |
| `i`    | 32 bits | little-endian   |
| `I`    | 32 bits | big-endian      |
| `n`    | 32 bits | host native     |
| `w`    | 64 bits | little-endian   |
| `W`    | 64 bits | big-endian      |
| `m`    | 64 bits | host native     |

**Floating-point specifiers** (IEEE 754; `format` accepts the
strings `NaN`, `Inf`, and `-Inf` per
[R-45917-21828](docs/public/tcl_language_standard_v1.md)):

| Letter | Width   | Byte order      |
|--------|---------|-----------------|
| `f`    | 32 bits | host native     |
| `r`    | 32 bits | little-endian   |
| `R`    | 32 bits | big-endian      |
| `d`    | 64 bits | host native     |
| `q`    | 64 bits | little-endian   |
| `Q`    | 64 bits | big-endian      |

**Bigint specifiers (TH8 extension; gate: `TH8_ENABLE_BIGINT`):**

| Letter | Width             | Byte order      |
|--------|-------------------|-----------------|
| `j`    | *count* bytes     | little-endian   |
| `J`    | *count* bytes     | big-endian      |

`binary format jN $bigint` and `binary format JN $bigint` import
the full bit pattern of an arbitrary-precision integer into a
fixed *N*-byte field, two's-complement signed.  `binary scan` of
`j` / `J` reads the *N* bytes back into a `bigint`-typed value,
sign-extending from the field width.  Unlike `w` / `W` (which
truncate the value to 64 bits modulo 2^64), `j` and `J` carry
**every bit** of the source bigint round-trip.  With `*` count on
`binary format j*`, TH8 uses the natural minimum byte width
required to represent the value in signed two's complement
(matching Python's `int.to_bytes(... signed=True)` semantics).

Maximum field count is `TH8_BINARY_MAX_COUNT = 0x3FFFFFFF` bytes
per field (defined in `src/plugins/th8_binary.c`).

**String specifiers:**

| Letter | Effect                                                            |
|--------|-------------------------------------------------------------------|
| `a`    | NUL-padded byte string of *count* bytes                            |
| `A`    | Space-padded byte string of *count* bytes; on `scan A`, trailing spaces AND NUL bytes are stripped |

**Bit specifiers:**

| Letter | Bit ordering within byte                       |
|--------|------------------------------------------------|
| `b`    | low-bit-first  (`0b00000001` = "1000 0000")    |
| `B`    | high-bit-first (`0b00000001` = "0000 0001")    |

**Hex specifiers:**

| Letter | Nibble ordering within byte           |
|--------|---------------------------------------|
| `h`    | low-nibble-first  (`0x12` = "21")     |
| `H`    | high-nibble-first (`0x12` = "12")     |

**Cursor specifiers** (no value consumed):

| Letter | Effect                                                                     |
|--------|----------------------------------------------------------------------------|
| `x`    | Pad forward *count* bytes (or 1 if count omitted) with NUL                 |
| `X`    | Rewind cursor by *count* bytes (or 1 if count omitted); count=`*` rewinds to position 0 |
| `@`    | Seek absolute to byte *count*; no `*` count permitted                      |

#### The `*` count

`*` on a `binary format` integer or float specifier consumes one
list argument and packs every list element.  `*` on `binary
scan` reads all remaining bytes into a list and assigns the list
to the next variable.

`*` on the cursor specifiers `x` / `@` is a hard error
(`cannot use "*" in binary format field`).  `*` on `X` rewinds
to the start.  `*` on `a` / `A` consumes one argument and emits
exactly its bytes (no padding or truncation).

#### Errors

  * Unknown format letter: `bad field specifier "X"` where the
    inserted letter falls back to `?` if outside printable ASCII.
  * Wrong number of value arguments for the format: `not enough
    arguments for all format specifiers`.
  * Mid-list parse error (e.g. `binary format i* {1 garbage 3}`):
    returns the underlying `Th8_ToWideInt` / `Th8_ToDouble`
    parse-error message.

### Control Flow

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `break`       | Exit innermost loop                             |
| `catch`       | `catch script ?resultVar? ?optionsVar?`         |
| `continue`    | Skip to next loop iteration                     |
| `coroutine`   | `coroutine name command ?arg ...?`              |
| `error`       | `error message ?info? ?code?`                   |
| `eval`        | `eval arg ?arg ...?`                            |
| `exit`        | `exit ?returnCode?`                             |
| `if`          | `if expr1 ?then? body1 ?elseif ...? ?else bodyN?` |
| `return`      | `return ?-code code? ?-level n? ?result?`       |
| `subst`       | `subst ?-nobackslashes? ?-nocommands? ?-novariables? string` |
| `switch`      | `switch ?options? string pattern body ...`       |
| `try`         | `try body ?on code varList body? ... ?finally body?` |
| `yield`       | `yield ?value?`                                 |

### Expressions

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `expr`        | `expr arg ?arg ...?`                            |
| `fpclassify`  | `fpclassify value` -- IEEE 754 classification   |

### Extensibility

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `load`        | `load fileName ?interp? ?packageName?`          |
| `package`     | See subcommands below                           |
| `unload`      | `unload ?-nocomplain? ?-keeplibrary? fileName ?interp? ?packageName?` |

**`package` subcommands:** `forget`, `ifneeded`, `names`, `present`,
`provide`, `require`, `unknown`, `vcompare`, `versions`, `vsatisfies`

### File Systems

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `cd`          | `cd ?dirName?`                                  |
| `file`        | See subcommands below                           |
| `pwd`         | Return current working directory                |
| `source`      | `source fileName`                               |

**`file` subcommands:** `channels`, `dirname`, `exists`, `extension`,
`join`, `nativename`, `normalize`, `pathtype`, `rootname`, `rootpath`,
`same`, `separator`, `split`, `tail`, `tempname`, `type`, `under`,
`validname`

### Formatting

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `format`      | `format formatString ?arg ...?`                 |
| `scan`        | `scan string format ?varName ...?`              |

### Introspection

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `info`        | See subcommands below                           |
| `pid`         | Return process ID                               |

**`info` subcommands:** `args`, `body`, `default`, `cmdcount`,
`commands`, `complete`, `context`, `exists`, `functions`, `globals`,
`level`, `library`, `loaded`, `plugins`, `nameofexecutable`,
`patchlevel`, `procs`, `script`, `sharedlibextension`, `subcommands`,
`varlinks`, `vars`

### I/O

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `close`       | `close channelId`                               |
| `flush`       | `flush channelId`                               |
| `gets`        | `gets channelId ?varName?`                      |
| `puts`        | `puts ?-nonewline? ?channelId? string`          |
| `read`        | `read ?-nonewline? channelId` / `read channelId numChars` |
| `seek`        | `seek channelId offset ?origin?`                |
| `tell`        | `tell channelId`                                |

### Lists

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `dict`        | See subcommands below                           |
| `join`        | `join list ?joinString?`                        |
| `lappend`     | `lappend varName ?value ...?`                   |
| `lassign`     | `lassign list ?varName ...?`                    |
| `lindex`      | `lindex list ?index ...?`                       |
| `list`        | `list ?arg ...?`                                |
| `llength`     | `llength list`                                  |
| `lrange`      | `lrange list first last`                        |
| `lremove`     | `lremove list ?index ...?`                      |
| `lreplace`    | `lreplace list first last ?element ...?`        |
| `lreverse`    | `lreverse list`                                 |
| `lsearch`     | `lsearch ?options? list pattern`                |
| `lsort`       | `lsort ?options? list`                          |
| `split`       | `split string ?splitChars?`                     |

**`dict` subcommands:** `append`, `create`, `exists`, `filter`, `for`,
`get`, `incr`, `info`, `keys`, `lappend`, `map`, `merge`, `remove`,
`replace`, `set`, `size`, `unset`, `update`, `values`, `with`

### Looping

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `for`         | `for start test next body`                      |
| `foreach`     | `foreach varList list ?varList list ...? body`   |
| `while`       | `while test body`                               |

### Management

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `interp`      | `interp cancel ?-unwind? ?--? ?result?`         |
| `namespace`   | See subcommands below                           |
| `rename`      | `rename oldName newName`                        |

**`interp` subcommands:** `cancel`

**`namespace` subcommands:** `children`, `code`, `current`, `delete`,
`eval`, `exists`, `export`, `import`, `parent`

### Procedures

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `apply`       | `apply lambdaExpr ?arg ...?`                    |
| `downlevel`   | `downlevel ?level? arg ?arg ...?`               |
| `napply`      | `napply ns lambdaExpr ?arg ...?` (named-namespace apply) |
| `nproc`       | `nproc ns name args body` (named-namespace proc) |
| `proc`        | `proc name args body`                           |
| `tailcall`    | `tailcall command ?arg ...?`                    |

### Strings

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `base64`      | `base64 encode|decode data`                     |
| `concat`      | `concat ?arg ...?`                              |
| `string`      | See subcommands below                           |

**`string` subcommands:** `compare`, `equal`, `first`, `index`, `is`,
`last`, `length`, `map`, `match`, `range`, `repeat`, `replace`,
`tolower`, `totitle`, `toupper`, `trim`, `trimleft`, `trimright`,
`wordend`, `wordstart`, `bytelength`, `reverse`

### Timekeeping

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `after`       | `after ms`                                      |
| `clock`       | See subcommands below                           |
| `time`        | `time script ?count?`                           |

**`clock` subcommands:** `seconds`, `https`, `ntp`

### Variables

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `append`      | `append varName ?value ...?`                    |
| `array`       | See subcommands below                           |
| `global`      | `global ?varName ...?`                          |
| `incr`        | `incr varName ?increment?`                      |
| `set`         | `set varName ?newValue?`                        |
| `unset`       | `unset ?-nocomplain? ?--? ?varName ...?`        |
| `uplevel`     | `uplevel ?level? arg ?arg ...?`                 |
| `upvar`       | `upvar ?level? otherVar myVar ?...?`            |
| `variable`    | `variable ?name value...? name ?value?`         |

**`array` subcommands:** `exists`, `get`, `names`, `set`, `size`, `unset`

### Regular Expressions

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `regexp`      | `regexp ?switches? exp string ?matchVar? ?subMatchVar ...?` |
| `regsub`      | `regsub ?switches? exp string subSpec ?varName?` |

### Cryptography and Security

| Command       | Synopsis                                        |
|---------------|-------------------------------------------------|
| `hash`        | `hash algorithm data`                           |
| `secure`      | `secure subcommand ...`                         |
| `harpy`       | See subcommands below                           |
| `flags`       | `flags ?name?`                                  |

**`harpy` subcommands:** `sign`, `verify`

---

## 5. Math Functions (in `expr`)

All 60+ functions available inside `expr`:

| Category        | Functions                                                              |
|-----------------|------------------------------------------------------------------------|
| Trigonometric   | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`                  |
| Hyperbolic      | `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`                     |
| Exponential/Log | `exp`, `exp2`, `expm1`, `log`, `log2`, `log10`, `log1p`, `logb`, `ldexp` |
| Power/Root      | `pow`, `sqrt`, `cbrt`, `hypot`                                        |
| Rounding        | `ceil`, `floor`, `round`, `trunc`                                     |
| Remainder       | `fmod`, `remainder`                                                   |
| Special         | `erf`, `erfc`, `gamma`, `lgamma`                                      |
| Comparison      | `max`, `min`, `copysign`, `dim`, `nextafter`                          |
| Classification  | `fpclassify`, `isfinite`, `isinf`, `isnan`, `isnormal`, `issubnormal`, `isunordered`, `signbit` |
| Type conversion | `int`, `wide`, `double`, `bool`, `entier`                             |
| Type query      | `typeof`                                                              |
| Integer math    | `abs`, `isqrt`                                                        |
| Constants       | `pi`, `epsilon`                                                       |
| Random          | `rand`, `random`, `srand`                                             |

---

## 6. System Variables

### `::tcl_platform` Array

| Key              | Example Value | Description                             |
|------------------|---------------|-----------------------------------------|
| `engine`         | `TH8`         | Always "TH8"                           |
| `platform`       | `unix`        | `unix` or `windows`                     |
| `compileOptions` | (list)        | Active compile-time option names         |
| `patchLevel`     | `1.0.0`       | Version string                          |

### `::th8_security` Array

| Key              | Example Value    | Description                           |
|------------------|------------------|---------------------------------------|
| `algorithmName`  | `RSA-16384`      | `none` or `RSA-16384`                 |
| `policy`         | `signed-only`    | `none` or `signed-only`               |
| `publicKeyToken` | (hex string)     | Public key token                      |

### Other Globals

| Variable        | Description                                       |
|-----------------|---------------------------------------------------|
| `::errorInfo`   | Stack trace of the most recent error               |
| `::errorCode`   | Machine-readable error code list                   |

---

## 7. Test Commands (`::th8testlib::*`)

These commands are available when the test library is loaded
(`TH8SH_YES_TESTLIB=1`).

### Full List

| Command                | Purpose                                              |
|------------------------|------------------------------------------------------|
| `nop`                  | No-op (accepts and ignores all arguments)            |
| `evalfile`             | Source a file by path                                |
| `expansion`            | Register/unregister custom expansion tags            |
| `isotime`              | Convert epoch seconds to ISO 8601 string             |
| `freeze`               | Suspend the interpreter                              |
| `freezecycle`          | Freeze, run script, thaw cycle                       |
| `freezequeuethaw`      | Freeze, queue script, thaw                           |
| `queuescript`          | Queue a script for deferred execution                |
| `issuspended`          | Check if interpreter is suspended                    |
| `preeval`              | Install/uninstall pre-evaluation hooks               |
| `preeval_denytest`     | Test pre-eval deny behavior                          |
| `preload`              | Install/uninstall pre-load hooks                     |
| `thaw`                 | Resume a frozen interpreter                          |
| `nulleval`             | Evaluate script with null I/O platform               |
| `utf8validate`         | Validate a string as UTF-8                           |
| `basepath`             | Return the base path of the test suite               |
| `test_lifecycle`       | Exercise interpreter create/destroy lifecycle        |
| `subnormal`            | Subnormal float detection and classification         |
| `inject_double`        | Inject a double value by raw uint64 bits             |
| `cancel_recover`       | Cancel interpreter and recover                       |
| `symlink`              | Create/query symbolic links                          |
| `isadmin`              | Check if running with elevated privileges            |
| `fuzz`                 | Fuzzing harness entry point                          |
| `glob`                 | File globbing                                        |
| `load_key_file`        | Load a signing key from file                         |
| `load_snk`             | Load a .snk (strong name key) file                   |
| `chan`                  | Channel get/set/reset for stdin/stdout/stderr        |
| `harpy_token`          | Compute harpy certificate token for a file           |
| `key_token`            | Get public key token for a named key                 |
| `verify_sig`           | Verify a script's signature                          |
| `sig_hashes`           | Get signature hash details for a script              |
| `policy_depth_test`    | Test nested security policy depth                    |
| `signed_only`          | Install/enable/disable/query signed-only policy      |
| `sandbox`              | Evaluate script in isolated child with resource limits |
| `fault`                | Fault injection testing                              |
| `kv`                   | Key-value operations via platform xKeyValue callback |
| `debug`                | Script debugging: callbacks, breakpoints, stepping, frames |

### Key Commands in Detail

- **`sandbox {script}`** -- Creates an isolated child interpreter with
  resource limits and evaluates `script` inside it.
- **`fault eval {script} ?options?`** -- Fault injection: forces
  allocation failures etc. to test error paths.
- **`signed_only query`** -- Returns whether signed-only policy is active.
- **`key_token keyName`** -- Returns the public key token for the named
  embedded key.
- **`cancel_recover`** -- Cancels the interpreter and tests recovery.
- **`nulleval script`** -- Evaluates with null I/O platform (no
  stdin/stdout/stderr).
- **`debug callback install|remove|events|clear|count`** -- Install
  a debug callback that captures step/breakpoint events.
- **`debug breakpoint set script line`** -- Set a breakpoint.
- **`debug step into|over|out|none|get`** -- Control step mode.
- **`debug frames count|info idx`** -- Inspect call stack.
- **`debug eval frameIdx script`** -- Evaluate in frame context.
- **`kv op ?name? ?value?`** -- Dispatches key-value operations
  through `Th8_KeyValue`.  Op names are case-insensitive:
  `exists`, `list`, `get`, `set`, `unset`, `exists2`, `list2`,
  `get2`, `set2`, `unset2`.  The backend depends on which
  platform layer provides `xKeyValue` (env, SQLite, or custom).

---

## 8. Bridge Commands

For cross-engine testing, the bridge libraries provide:

| Command     | Direction         | Description                              |
|-------------|-------------------|------------------------------------------|
| `th8Eval`   | Tcl --> TH8       | Evaluate a TH8 script from native Tcl    |
| `tclEval`   | TH8 --> Tcl       | Evaluate a Tcl script from TH8           |

Built via `make bridge`.  Source: `src/test/th8_tcl.c`.

---

## 9. Coding Conventions

### Naming

- **Internal functions:** `th8Whatever` (lowercase `th8` prefix, camelCase)
- **Public API functions:** `Th8_Whatever` (capitalized prefix, underscore)
- **Exported symbols:** Must have `TH8_API` attribute
- Use `argv[0]` in diagnostic messages so renamed commands show their
  current name

### Return Types

- **Boolean yes/no:** plain `int` (1/0)
- **Success/failure:** `TH8_OK` / `TH8_ERROR`
- **Pointer-returning functions:** Return `NULL` on failure, never bare `0`

### Memory

- **`Th8_AttemptMalloc`** for all internal allocations (returns `NULL` on
  failure)
- **`Th8_Malloc`** only in the embedder-facing API (panics on failure)
- **`Th8_SetResultStatic`** for allocation-free error messages (string
  literals)
- Every function return value must be checked for failure, even if
  "impossible"

### Platform Layer Discipline

- Each platform file (`th8_posix.c`, `th8_win32.c`, `th8_macos.c`,
  `th8_libc.c`) must **only** use APIs from its own abstraction layer
- POSIX files use POSIX APIs, Win32 files use Win32 APIs, etc.
- Use `ConvertUTF_v2.c` (from Eagle) for all UTF-8 to OS encoding
  conversion in platform files

### Build System

- Always update `Makefile.msc` alongside `Makefile`
- Platform files go in `CORE_OBJ`, not plugin objects
- Use `CFLAGS_DLL` for shared library objects, `CFLAGS_APP` for
  executables
- Atomic CAS initialization pattern with short-circuit `&&`

### Consistency

- Abstractions must be perfectly consistent in usage across the entire
  codebase
- Related items must appear in the same order everywhere (headers,
  implementations, stubs, docs, tests)
- Update docs (standard, tests, paper) incrementally with each change,
  not in batches

---

## 10. Test File Conventions

### Structure

Every test file follows this pattern:

```tcl
source [file join [file dirname [info script]] prologue.tcl]

# ============================================================
# test-name-1.1 ...
# ============================================================

runTest {
    # R-XXXXX-XXXXX
    -body { ... }
    -result { ... }
    -cleanup { ... }
}

# ============================================================
# test-name-1.2 ...
# ============================================================

runTest {
    # R-XXXXX-XXXXX
    -body { ... }
    -result { ... }
    -cleanup { ... }
}

source [file join [file dirname [info script]] epilogue.tcl]
```

### Rules

- Every test uses a `runTest` wrapper
- Every test has an R-marker reference
- Separator comments (`# ====...`) between every test
- `-cleanup` blocks for all procs and variables
- No inline helper procedures -- put them in
  `lib/Standard1.0/th8test.tcl`
- `prologue.tcl` / `epilogue.tcl` bookends (always sourced via
  `[file join [file dirname [info script]] ...]`)
- Use `regexp` for matching floating-point results in tests
- Conformance tests: no forking, one standard, R-markers on all tests

### After Modifying Test Scripts

```sh
bash tools/signScript.sh tests/<modified-file>.tcl
```

---

## 11. Architecture

### Platform Abstraction

The `Th8_Platform` struct provides ~67 callbacks that abstract all OS
interaction.  The interpreter never calls OS APIs directly; everything
routes through these callbacks.

### Per-Callback Context

Each callback receives a `pCtx` pointer.  By default this is the
platform's shared `pCtx`, but extensions can set per-callback
overrides via `Th8_SetPlatformContext(interp, xCallback, pCtx)`.
The dispatch layer resolves the context via a per-interpreter hash
table, falling back to the platform default if no override exists.
This enables different backends (POSIX, SQLite, etc.) to each have
their own context for the callbacks they provide.

### Platform Merge for Extensions

`Th8_MergePlatformInterp(interp, pSrc)` allows loadable extensions
to install platform callbacks into a running interpreter.  It clones
the current platform, merges non-NULL callbacks from `pSrc`, and
replaces the interpreter's platform pointer.  Used by the SQLite
extension to install its `xKeyValue` callback after loading.

### Three-Tier Visibility

| Tier             | Marker          | Scope                          |
|------------------|-----------------|--------------------------------|
| Public API       | `TH8_API`       | Exported to embedders          |
| Library-internal | `TH8_INTERNAL`  | Visible within libth8          |
| File-local       | `static`        | Visible within one `.c` file   |

### Dual Allocation Model

| Function            | On failure  | Use case                       |
|---------------------|-------------|--------------------------------|
| `Th8_Malloc`        | Panics      | Embedder-facing API only       |
| `Th8_AttemptMalloc` | Returns NULL | All internal allocations       |

### Compile-Time Modularity

- **14 plugins:** Control, Expressions, Extensibility, FileSystems,
  Formatting, Introspection, IO, Lists, Looping, Management, Procedures,
  Strings, Timekeeping, Variables
- **5 optional features:** Regexp, BigInt, Cryptography, Unbound, libcurl
- Plugins can be individually disabled:
  `make PLUGIN_LOOPING=0 PLUGIN_FORMATTING=0 fresh`

### Platform Layers (9 total)

| Layer         | File               | Purpose                                    |
|---------------|--------------------|--------------------------------------------|
| libc          | `th8_libc.c`       | C runtime bridge (mem, I/O wrappers)       |
| null-I/O      | `th8_nullio.c`     | Restricted I/O (all I/O ops denied)        |
| POSIX         | `th8_posix.c`      | Linux, BSD, macOS OS interaction            |
| Win32         | `th8_win32.c`      | Windows OS interaction                      |
| macOS         | `th8_macos.c`      | macOS-specific extensions (atop POSIX)      |
| curl          | `th8_curl.c`       | libcurl-backed NTP/HTTPS                   |
| mimalloc      | `th8_mimalloc.c`   | Microsoft mimalloc allocator               |
| cosmopolitan  | `th8_cosmopolitan.c` | Cosmopolitan Libc portability layer       |
| fault         | `th8_fault.c`      | Fault injection (wraps another platform)   |
| env           | `th8_env.c`        | Environment variable key-value store       |

---

## 12. File System and I/O Philosophy

TH8 has **no built-in commands that directly access the host system**.
Every external interaction --- file reads, console I/O, path resolution,
binary loading --- is mediated by the `Th8_Platform` callback table.
The embedder controls what exists, what can be read, and where output
goes.

### Virtual File System: `xGetData` / `xDataExists`

`[source]` and `[file exists]` do not open files directly.  They
invoke abstract platform callbacks:

- `xGetData(interp, pCtx, zName, nName, &pzOut, &pnOut)` ---
  Retrieve named data.  The platform decides what "names" mean:
  filesystem paths, repository artifacts, HTTP resources, or
  embedded resource tables.
- `xDataExists(interp, pCtx, zName, nName, &pAttrs)` --- Test
  whether a named datum exists and return file-type attributes.

This means `[source init.tcl]` works identically whether the data
comes from a POSIX filesystem, a Fossil repository, or a compiled-in
byte array.  The script never knows the difference.

### Base-Path Sandbox (POSIX)

All POSIX file operations are confined to a **base directory**:

1. Auto-detected at startup via `dladdr()` (locates the TH8 library
   itself) or set explicitly via `Th8_SetBasePath()`.
2. **Absolute paths are rejected** --- `if (zPath[0] == '/') return 0`.
3. **`..` traversal is resolved and checked** --- even for
   non-existent paths, manual segment resolution prevents escape.
4. **Symlinks are resolved** via `realpath()` before the base-path
   check, preventing symlink-based escapes.
5. `[cd]` only accepts `.` or paths that resolve under base.
6. `[pwd]` reports paths relative to base (e.g., `./lib`).

### Read-Only by Design

The platform callback table deliberately **omits**:
`xMkdir`, `xRmdir`, `xUnlink`, `xRename`, `xStat`.

TH8 cannot create, delete, rename, or modify files.  The only
write path is temporary data (`[file tempname]`), which is managed
entirely by the platform and can be vetoed on close.

### Null I/O Platform

`th8_nullio.c` provides a **zero-capability baseline**:

| Callback | Behavior |
|----------|----------|
| `xGetData` | Returns 0 bytes (empty script for `[source]`) |
| `xDataExists` | Always returns false |
| `xLoad` / `xUnload` | Returns error ("forbidden") |
| `xInput` | Returns 0 bytes (immediate EOF) |
| `xOutput` / `xOutputError` | Accepts and discards (no-op) |
| `xNormalizePath` | Returns path verbatim |
| `xGetCwd` | Returns `.` |

A null-I/O interpreter can run pure computation without errors on
I/O commands, but cannot read, write, or load anything.  Real
capabilities are added selectively via `Th8_MergePlatform()`.

### Key-Value Store: `xKeyValue`

The `xKeyValue` callback provides a general-purpose key-value
abstraction with ten operations:

**Literal-key operations (0-4):**

- `TH8_KV_EXISTS` -- check if a key exists (literal name)
- `TH8_KV_LIST` -- glob-match keys, return name list
- `TH8_KV_GET` -- retrieve value (literal name)
- `TH8_KV_SET` -- store key-value pair (both literal)
- `TH8_KV_UNSET` -- remove a key (literal name)

**Bulk glob operations (5-9):**

- `TH8_KV_EXISTS2` -- check if any key matches name/value globs
- `TH8_KV_LIST2` -- list keys matching name AND/OR value globs
- `TH8_KV_GET2` -- return dict of matching key-value pairs
- `TH8_KV_SET2` -- set all matching keys to a literal value
- `TH8_KV_UNSET2` -- remove matches, return dict of deleted pairs

For *2 ops: NULL zName matches all keys, NULL zValue matches all
values.  Glob matching uses `Th8_GlobMatch` (same as
`[string match]`).  SET2/UNSET2 collect matches before mutation.

The `th8_env.c` platform layer backs this with environment
variables (POSIX: `getenv`/`setenv`; Win32: wide-char APIs with
ConvertUTF_v2 strict validation).  Embedders can provide
alternative backends (databases, registries, etc.) by
implementing the same callback.

### Compile-Time Removal

Entire capability categories vanish at compile time:

- Without `TH8_PLUGIN_FILE_SYSTEMS`: no `cd`, `pwd`, `file`, `source`
- Without `TH8_PLUGIN_IO`: no `puts`, `gets`, `read`, `close`, etc.
- Without `TH8_ENABLE_LOAD`: no `load`, `unload`, `package`

The result is not hidden commands (Tcl safe interp model) but
**absent commands** --- they do not exist in the interpreter at all.

### Security Stack

```
Script:       [source init.tcl]
                  |
C API:        Th8_EvalFile -> Th8_GetData
                  |
Policy:       Th8_PolicyProc (PRE|READ) --- verify signature
                  |
Platform:     xGetData --- base-path check, open, read
                  |
OS:           open() + read() (POSIX) or embedder-supplied backend
```

Every layer can reject the operation.  The policy callback can
verify the cryptographic signature of the raw bytes before the core
interpreter ever sees the script text.

---

## 13. Eagle's Influence on TH8

[Eagle](https://eagle.to/) is an implementation of Tcl written
entirely in managed C# for the CLR.  It has been in production
for 18+ years.  TH8's architect is also the creator of Eagle,
and several Eagle concepts are foundational to TH8's design.
Together with TH1 (the embeddability proof) and Tcl 8.x (the
language contract), Eagle completes TH8's three-ancestor lineage
by providing the security architecture and production discipline.

### Directly Adopted

**Spilornis list parser.**  TH8 uses Eagle's battle-tested list
parser (`externals/spilornis/`) rather than reimplementing list
splitting.  A UTF-8 bridge header (`src/th8_spilornis.h`) adapts
Spilornis's wide-character API to TH8's byte-oriented model with
zero CRT linkage.  LLM-assisted code review and AFL++ fuzzing
of the Spilornis integration uncovered a buffer overflow in UTF-8
backslash expansion (24 concrete crash inputs), leading to fixes
in both TH8's copy and the upstream library.

**Named procedures: `nproc` and `napply`.**  Eagle's keyword
argument extensions appear directly in TH8.  `nproc` defines
procedures with named/keyword parameters; `napply` applies
lambdas with the same semantics.  `downlevel` (the complement
to `uplevel`) is also Eagle-derived.

**ConvertUTF_v2.**  All UTF-8 to UTF-16/UTF-32 conversions use
the vendored ConvertUTF_v2 library (from Eagle) with strict
validation mode.  Invalid UTF-8 sequences are rejected rather
than silently replaced --- a security property.

**Structured return codes.**  TH8's return code model
(`TH8_OK`, `TH8_ERROR`, `TH8_RETURN`, `TH8_BREAK`,
`TH8_CONTINUE`) mirrors Eagle's `ReturnCode` enum, providing
clean distinction between error conditions and control flow
throughout the evaluation chain.

### Adapted (Not Directly Copied)

**Harpy cryptographic signing.**  Eagle's Harpy XML certificate
system inspired TH8's signed-only script policy.  TH8 implements
a simpler two-tier model: `.b64sig` bootstrap signatures (raw
RSA-SHA512 in base64) for development, and the full `.harpy` XML
certificate format for commercial deployment.  The unified
`Th8_PolicyProc` callback (with `PRE|READ`, `PRE|EVAL`,
`POST|EVAL` phases) is TH8-specific but achieves the same
security guarantee: only cryptographically verified scripts
execute.

**Interpreter cancellation.**  Eagle's `cancel` command became
TH8's `interp cancel` with two modes: normal (sets a flag
checked at work-unit boundaries via `Th8_Ready()`) and unwind
(prevents `catch` from intercepting the cancellation).  The
unwind mode enables reliable preemption of runaway scripts from
another thread.

**Non-recursive engine (NRE).**  TH8's trampoline-driven
evaluation engine is inspired by Miguel Sofer's NRE work in
Tcl 8.6, which Eagle also adopted.  The NRE design enables
coroutines, tail calls, and suspension (`Th8_Freeze`/`Th8_Thaw`)
without consuming C stack --- critical for agentic use cases
where many interpreters run concurrently.

**Platform abstraction philosophy.**  While not invented by
Eagle, the idea that the embedder controls every external
interaction via a callback table reflects Eagle's
architecture-aware approach.  TH8 extends this further: 66
callbacks organized into 16 logical groups, with 9 composable
platform layers, and zero direct CRT calls in non-platform code.

### Eagle as Correctness Oracle

TH8 uses Eagle as a **reference implementation** for ambiguous
Tcl semantics.  The project's formal correctness rules:

- Tcl 8.4 behavior is always correct for standard Tcl features.
- Eagle behavior is always correct for Eagle-specific extensions
  (`nproc`, `napply`, `downlevel`, keyword arguments).
- Where Tcl 8.4 and Eagle agree, that behavior is normative.
- Where they disagree, the Tcl Language Standard arbitrates.

The test suite runs against both Tcl and Eagle to verify
conformance.  `doc/eagle-tests.txt` records Eagle conformance
results (e.g., `downlevel`: 31/31 passed, `catch`: 26/26 passed).

### Philosophical Alignment

| Dimension | Eagle | TH8 |
|-----------|-------|-----|
| Default capabilities | Zero (callback-controlled) | Zero (platform-controlled) |
| Security boundary | Managed code + callbacks | Platform abstraction layer |
| Untrusted code handling | 18 years production use | Safe-by-construction sandbox |
| Script signing | Harpy XML certificates | `.b64sig` + Harpy (planned) |
| Resource limits | Step counter, depth limits | Step, memory, result size, stack, depth |
| String model | Everything is a string | Everything is a string + taint bits |
| Error model | ReturnCode enum | TH8_OK / TH8_ERROR / ... |
| Formal spec | Proven behavior (18 years) | Tcl Language Standard v1 (854 reqs) |

The key distinction: Eagle is a **safe interpreter** (capabilities
are controlled at runtime by the host).  TH8 is a **safe-by-construction
interpreter** (capabilities are absent unless the platform provides
them, and entire command categories can be removed at compile time).
Both achieve embedder-controlled security, but through different
architectural mechanisms.

---

## 14. Compile-Time Feature Gates

### Feature Defines (`TH8_ENABLE_*`)

| Define                          | Controls                                  |
|---------------------------------|-------------------------------------------|
| `TH8_ENABLE_REGEXP`            | PostgreSQL Spencer regex engine            |
| `TH8_ENABLE_BIGINT`            | Arbitrary-precision integers              |
| `TH8_ENABLE_CRYPTOGRAPHY`      | RSA signing, harpy certs, key management  |
| `TH8_ENABLE_UNBOUND`           | DNSSEC-validated DNS via libunbound       |
| `TH8_ENABLE_LIBCURL`           | libcurl HTTP/NTP client                   |
| `TH8_ENABLE_EXPRESSIONS`       | Expression engine (cascades to plugins)   |
| `TH8_ENABLE_VARIABLES`         | Variable system                           |
| `TH8_ENABLE_LOAD`              | Dynamic library loading                   |
| `TH8_ENABLE_TEST_KEY`          | Test signing key (for test suite)         |
| `TH8_ENABLE_FAULT_INJECTION`   | Fault injection platform layer            |

### Plugin Defines (`TH8_PLUGIN_*`)

| Define                          | Plugin                                    |
|---------------------------------|-------------------------------------------|
| `TH8_PLUGIN_CONTROL`           | break, catch, continue, if, switch, etc.  |
| `TH8_PLUGIN_EXPRESSIONS`       | expr, fpclassify                          |
| `TH8_PLUGIN_EXTENSIBILITY`     | load, package, unload                     |
| `TH8_PLUGIN_FILE_SYSTEMS`      | cd, file, pwd, source                     |
| `TH8_PLUGIN_FORMATTING`        | format, scan                              |
| `TH8_PLUGIN_INTROSPECTION`     | info, pid                                 |
| `TH8_PLUGIN_IO`                | close, flush, gets, puts, read, seek, tell |
| `TH8_PLUGIN_LISTS`             | dict, join, lappend, list, lsort, etc.    |
| `TH8_PLUGIN_LOOPING`           | for, foreach, while                       |
| `TH8_PLUGIN_MANAGEMENT`        | interp, namespace, rename                 |
| `TH8_PLUGIN_PROCEDURES`        | apply, proc, nproc, napply, tailcall      |
| `TH8_PLUGIN_STRINGS`           | base64, concat, string                    |
| `TH8_PLUGIN_TIMEKEEPING`       | after, clock, time                        |
| `TH8_PLUGIN_VARIABLES`         | append, array, global, incr, set, etc.    |

### Platform Defines (`TH8_PLATFORM_*`)

| Define                          | Auto-detected          | Purpose              |
|---------------------------------|------------------------|----------------------|
| `TH8_PLATFORM_POSIX`           | Non-Windows            | POSIX layer          |
| `TH8_PLATFORM_WIN32`           | `_WIN32`               | Win32 layer          |
| `TH8_PLATFORM_MACOS`           | `__APPLE__` + POSIX    | macOS extensions     |
| `TH8_PLATFORM_LIBC`            | Always                 | C runtime bridge     |
| `TH8_PLATFORM_NULLIO`          | Always                 | Null I/O layer       |
| `TH8_PLATFORM_CURL`            | With `TH8_ENABLE_LIBCURL` | curl layer       |
| `TH8_PLATFORM_COSMOPOLITAN`    | Manual                 | Cosmopolitan Libc    |

### Allocator / UI Defines (`TH8_USE_*`)

| Define                          | Purpose                                   |
|---------------------------------|-------------------------------------------|
| `TH8_USE_MIMALLOC`             | Use mimalloc allocator platform            |
| `TH8_USE_BESTLINE`             | Use bestline readline replacement          |

### Dependency Cascade

Disabling a core feature automatically disables dependent plugins:

- `ENABLE_EXPRESSIONS=0` disables: Control, Expressions, Formatting,
  Looping
- `ENABLE_VARIABLES=0` disables: Variables plugin
- `ENABLE_LOAD=0` disables: Extensibility plugin

---

## 15. Key Source Files

### Core Interpreter

| File                    | Purpose                                         |
|-------------------------|-------------------------------------------------|
| `src/th8.h`            | Public API header (all exported types/functions) |
| `src/th8_int.h`        | Internal header                                 |
| `src/th8_int_core.h`   | Core internal header                            |
| `src/th8_core.c`       | Core interpreter engine                         |
| `src/th8_lang.c`       | Language evaluation (parser, substitution)       |
| `src/th8_expr.c`       | Expression engine                               |
| `src/th8_vars.c`       | Variable system                                 |
| `src/th8_util.c`       | Utility functions                               |
| `src/th8_cache.c`      | Command/result caching                          |
| `src/th8_channel.c`    | I/O channel abstraction                         |
| `src/th8_load.c`       | Dynamic library loading                         |
| `src/th8_plugin.c`     | Plugin registration framework                   |
| `src/th8_math.c`       | Math function implementations                   |
| `src/th8_glob.c`       | Glob pattern matching                           |
| `src/th8_hash.c`       | Hash table implementation                       |
| `src/th8_bigint.c`     | Big integer support (libtommath bridge)         |
| `src/th8_base64.c`     | Base64 encode/decode                            |
| `src/th8_xlib.c`       | Extended library routines                       |

### Platform Files

| File                    | Layer                                           |
|-------------------------|-------------------------------------------------|
| `src/th8_plat.c`       | Platform dispatch / layered composition          |
| `src/th8_plat.h`       | Platform struct pre-declarations                 |
| `src/th8_libc.c`       | C runtime bridge                                |
| `src/th8_nullio.c`     | Null I/O platform                               |
| `src/th8_posix.c`      | POSIX platform (Linux, BSD, macOS)              |
| `src/th8_win32.c`      | Windows platform                                |
| `src/th8_macos.c`      | macOS-specific extensions                       |
| `src/th8_curl.c`       | libcurl platform                                |
| `src/th8_mimalloc.c`   | mimalloc allocator platform                     |
| `src/th8_cosmopolitan.c` | Cosmopolitan Libc platform                    |
| `src/th8_fault.c`      | Fault injection platform (wraps another)        |
| `src/th8_env.c`        | Environment variable key-value platform          |
| `src/th8_spilornis.c`  | Spilornis bridge helper (isspace)               |
| `src/th8_spilornis.h`  | Spilornis type overrides (se_* prefixed)        |
| `src/th8_protect.c`    | Protected/locked memory allocation              |
| `src/sqlite3/th8_sqlite3.c` | SQLite key-value extension (loadable)       |
| `src/sqlite3/th8_sqlite3.h` | SQLite extension export header              |

### Plugins (`src/plugins/`)

| File                        | Commands                                    |
|-----------------------------|---------------------------------------------|
| `th8_control.c`             | break, catch, continue, coroutine, error, eval, exit, if, return, subst, switch, try, yield |
| `th8_expressions.c`        | expr, fpclassify                            |
| `th8_extensibility.c`      | load, package, unload                       |
| `th8_filesystems.c`        | cd, file, pwd, source                       |
| `th8_formatting.c`         | format, scan                                |
| `th8_introspection.c`      | info, pid                                   |
| `th8_io.c`                 | close, flush, gets, puts, read, seek, tell  |
| `th8_lists.c`              | dict, join, lappend, lassign, lindex, list, llength, lrange, lremove, lreplace, lreverse, lsearch, lsort, split |
| `th8_looping.c`            | for, foreach, while                         |
| `th8_management.c`         | interp, namespace, rename                   |
| `th8_procedures.c`         | apply, downlevel, napply, nproc, proc, tailcall |
| `th8_strings.c`            | base64, concat, string                      |
| `th8_timekeeping.c`        | after, clock, time                          |
| `th8_variables.c`          | append, array, global, incr, set, unset, uplevel, upvar, variable |

### Crypto / Security (`src/plugins/crypto/`, `src/plugins/harpy/`)

| File                        | Purpose                                     |
|-----------------------------|---------------------------------------------|
| `crypto/th8_crypto_cmds.c`  | hash command                               |
| `crypto/th8_secure.c`       | secure command                             |
| `harpy/th8_harpy.c`         | Harpy certificate handling                 |
| `harpy/th8_snk.c`           | Strong name key (.snk) parsing             |
| `harpy/th8_policy.c`        | Script security policy engine              |
| `harpy/th8_keyRoot.c`       | Root signing key (RSA-16384)               |
| `harpy/th8_key0.c`          | Key 0 (bootstrap key)                      |
| `harpy/th8_keyTest.c`       | Test signing key                           |
| `harpy/th8_keyTime.c`       | Time-based key                             |
| `harpy/th8_attrflags.c`     | Attribute flags for certificates           |
| `harpy/th8_time.c`          | Time validation for certificates           |

### Regex (`src/plugins/regexp/`)

| File                        | Purpose                                     |
|-----------------------------|---------------------------------------------|
| `th8_regex.c`               | regexp/regsub commands (Spencer engine)     |
| `regex_th8.h`               | Regex engine configuration                 |

### Stubs

| File                    | Purpose                                         |
|-------------------------|-------------------------------------------------|
| `src/th8Decls.h`       | Generated stubs declarations                    |
| `src/th8StubInit.c`    | Generated stubs initialization table            |
| `src/th8StubLib.c`     | Stubs library for extensions                    |

### Shell

| File                    | Purpose                                         |
|-------------------------|-------------------------------------------------|
| `src/th8sh.c`          | Interactive shell (th8sh)                       |
| `src/th8_fossil.c`     | Fossil integration layer                        |
| `src/th8_fossil.h`     | Fossil integration header                       |

### Test Infrastructure

| File                           | Purpose                                  |
|--------------------------------|------------------------------------------|
| `src/test/th8_testlib.c`      | C test library (all `::th8testlib::*` cmds) |
| `src/test/th8_testlib.h`      | Test library header                      |
| `src/test/th8_tcl.c`          | Tcl bridge (tclEval / th8Eval)           |
| `src/test/th8_tcl.h`          | Tcl bridge header                        |
| `lib/Standard1.0/test.tcl`    | Test framework (runTest etc.)            |
| `lib/Standard1.0/exec.tcl`    | Exec utilities                           |
| `lib/Standard1.0/load.tcl`    | Package loading                          |
| `tests/prologue.tcl`          | Test suite prologue                      |
| `tests/epilogue.tcl`          | Test suite epilogue                      |
| `tests/all.tcl`               | Test suite runner                        |

### Tools

| File                    | Purpose                                         |
|-------------------------|-------------------------------------------------|
| `tools/mkreq.tcl`      | R-marker generator (MD5-based)                  |
| `tools/mkstubs.tcl`    | Stubs table generator                           |
| `tools/mkversion.tcl`  | Version header generator                        |
| `tools/mkkey.tcl`      | Key generation tool                             |
| `tools/signScript.sh`  | Script signing (bash wrapper)                   |
| `tools/signScript.th8` | Script signing (TH8 implementation)             |
| `tools/mkamal.tcl`     | Amalgamation builder (th8.c + th8.h)            |
| `tools/mkspilornis.tcl`| Spilornis type rename (se_* prefix)             |
| `tools/tommath_amalg.tcl`| libtommath amalgamation builder               |
| `tools/regex_amalg.tcl`| Spencer regex amalgamation builder              |
| `tools/regex_vendor.tcl`| Spencer regex patching/vendoring               |
| `tools/build-matrix.sh`| Compile option combination testing              |
| `tools/full-build.sh`  | Full build script (all configurations)          |

### Documentation

| File                                  | Purpose                          |
|---------------------------------------|----------------------------------|
| `docs/public/tcl_language_standard_v1.md`     | Formal language specification    |
| `docs/public/th8_public_c_api_specification.md` | C embedding API reference      |
| `docs/private/ladybird.md`                    | LadyBird browser integration     |
| `docs/public/tcl_conference_paper.md`    | Conference paper                 |
| `license.terms`                       | License file                     |

### Keys

| File              | Purpose                                              |
|-------------------|------------------------------------------------------|
| `keys/keyRoot.snk` | Root signing key (RSA-16384)                        |
| `keys/key0.snk`    | Bootstrap key                                       |
| `keys/keyTime.snk` | Time-based key                                      |

---

## 16. Quick Reference: Common Tasks

### Add a new built-in command

1. Implement in the appropriate plugin file under `src/plugins/`
2. Register in the plugin's init function
3. Add requirement text with R-marker to `doc/tcl_language_standard_v1.md`
4. Generate R-marker: `tclsh tools/mkreq.tcl "requirement text"`
5. Write tests in `tests/` with R-marker references
6. Update both `Makefile` and `Makefile.msc` if new files are needed
7. Rebuild: `make fresh ENABLE_TEST_KEY=1`
8. Run tests: `TH8SH_YES_TESTLIB=1 ./bin/th8sh tests/all.tcl`
9. Sign modified scripts: `bash tools/signScript.sh tests/<file>.tcl`

### Add a new platform callback

1. Add the callback to `Th8_Platform` in `src/th8_plat.h` / `src/th8.h`
2. Implement in each platform file (`th8_posix.c`, `th8_win32.c`, etc.)
3. Add dispatch wrapper in `src/th8_plat.c`
4. Rebuild with `make fresh`

### Add a new test file

1. Create `tests/<name>.tcl` with prologue/epilogue bookends
2. Add the filename alphabetically to `tests/all.tcl`
3. Sign: `bash tools/signScript.sh tests/<name>.tcl`
4. Re-sign `all.tcl`: `bash tools/signScript.sh tests/all.tcl`

### Regenerate stubs

Stubs are regenerated automatically by `make fresh` (via `genstubs`
target).  Stubs gating must be in `src/th8.h`, not hand-edited in
generated files.
