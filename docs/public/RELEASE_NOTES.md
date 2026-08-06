# TH8 1.0.0 Release Notes

TH8 is an embeddable Tcl-compatible scripting language with a
security-hardened C ABI, a small portable platform-callback
layer, and an optional code-signing gate suitable for shipping
production binaries.  This is the first stable release; the
language surface conforms to Tcl 8.6 with documented extensions,
and the C ABI is versioned via `TH8_STUBS_VERSION`.

## Supported platforms

TH8 1.0.0 builds and passes the full conformance test suite on:

- **macOS** (Darwin, both Apple Silicon and x86-64).  Reference
  platform; the CI matrix runs this configuration on every push
  and nightly.
- **Linux** (x86-64 and aarch64) via the POSIX platform backend.
- **Windows** (x86-64) via the Win32 platform backend
  (`Makefile.msc`, nmake or MSBuild).
- **Cosmopolitan / Actually Portable Executable** (Linux + macOS
  + Windows + FreeBSD + OpenBSD + NetBSD from a single binary)
  via `Makefile.cosmopolitan`.

Mobile platforms are supported but not fully CI-validated in
this release; treat them as Tier-2:

- **iOS** (arm64) -- builds via `TH8_TARGET=ios`; embed in an
  Xcode application target.
- **Android** (arm64-v8a, x86-64) -- builds via
  `TH8_TARGET=android`; embed in an NDK application via JNI.

A per-platform CI matrix and on-device test harnesses for iOS
and Android are tracked as post-1.0 work.

## Building

### Native (Unix-like)

On a fresh Debian/Ubuntu machine, install the system build
dependencies once, first (see also `docs/public/portability.md`):

    make apt-deps                    # build tools + tcl-dev + libssl/curl/unbound-dev
    make apt-deps-static             # ...plus the extras for a fully-static link

Both run `apt-get` via `sudo` (pass `SUDO=` to run as root directly).
Then build:

    make clean debug                 # debug + assertions
    make ENABLE_TEST_KEY=1 clean debug && bin/th8sh tests/all.tcl

The `make` target produces `bin/th8sh` plus the static
(`bin/libth8.a`) and dynamic (`bin/libth8.dylib` /
`libth8.so`) libraries.  The conformance suite passes with
**4376 / 0 / 0 / 9** (passed / failed / mutated / skipped) under
`ENABLE_TEST_KEY=1` on the reference platform (4385 test cases
total, of which 9 are gated on platform-conditional constraints).

### Native (Windows)

    nmake -f Makefile.msc
    bin\th8sh.exe tests\all.tcl

The Win32-only third-party static libraries (OpenSSL, zlib,
libcurl, Tcl 8.6 headers) are bootstrapped on demand by
`tools/bootstrap_win32_deps.sh` and built per the
`externals/<name>/README.md` instructions; nothing third-party
ships pre-built in the source tree.

### Amalgamated single-file build

    make amalgamation                # produces bin/th8.c + bin/th8.h
    make ENABLE_TEST_KEY=1 amalgamation-test

The amalgamation collapses the entire interpreter into a single
`bin/th8.c` (~141k lines including comments and blanks, ~71k
code lines; roughly 3.8 MB on disk) suitable for direct inclusion
in embedder build systems.

### Cosmopolitan Actually Portable Executable

    bash tools/bootstrap_cosmo.sh    # one-time toolchain install
    make cosmo                       # builds bin-cosmo/th8sh.com
    make cosmo-test                  # runs tests/all.tcl

The resulting `bin-cosmo/th8sh.com` is a single binary that
runs unmodified on Linux, macOS, Windows, FreeBSD, OpenBSD, and
NetBSD across both x86_64 and aarch64.

## Conformance and coverage

  * **Tcl 8.6 conformance:** **4385** conformance tests
    (4376 passed + 9 platform-conditional skipped, 0 failed,
    0 mutated) across 258 test files.  Every normative
    requirement in the language standard is tagged with an
    `R-NNNNN-NNNNN` requirement marker and cross-checked by
    `make audit-reqs`.

  * **R-marker coverage (all four spec documents):**
    **1110 of 1142 = 97.20%** specification requirements
    across `tcl_language_standard_v1.md`,
    `th8_language_extensions.md`,
    `th8_public_c_api_specification.md`, and
    `th8_internal_api_specification.md` are pinned by at
    least one test.  The 32 uncovered markers belong to
    five infrastructure-blocked groups (coding-guideline
    audit, signed-script annotation fixtures, Win32-only
    callbacks, internal-only APIs, signal-driven paths).
    `tools/mkreq.tcl --check-tests` invoked against the
    union of all four docs reports zero test-orphan
    markers; the audit gate in `make audit-reqs` enforces
    this on every build.  Test invocations citing R-markers
    total 2,481.

  * **MC/DC coverage:** **87.56%** lib-wide on the macOS
    reference platform.  The remaining missed conditions
    are concentrated in syscall / NTP / DNS fault-injection
    arms, signed-script annotation paths, and three
    Win32-only branches inside `plugins/th8_filesystems.c`.

  * **Audit gates:** `make audit` enforces a banned-pattern
    source scan (`tools/audit_patterns.tcl`), a CRT-object
    dependency check, and a clang-format conformance scan on
    every build.  All three are required to pass before merge.

## Cryptography and signed-only mode

TH8 ships with an **optional signed-only evaluation gate**
that, when enabled, requires every top-level script to carry a
valid RSA-PKCS#1-v1.5 + SHA-256 signature whose public-key
token is embedded in the interpreter's preloaded key set.  The
token mechanism is described in `th8_language_extensions.md`
section 29.

The gate is opt-in: a default-built interpreter accepts
unsigned scripts.  An embedder calls `Th8_EnableSignedOnly(...)`
during interpreter setup to lock down evaluation.

A **commercial signing service** is available separately for
enterprise customers who need a vetted key-management workflow;
contact the maintainer for details.  Open-source users can
sign their own scripts via `tools/signScript.sh` with
self-managed RSA keys; the three public-key trust anchors used
by the reference build ship under `keys/` and are documented
in `keys/README.md`.

## Known limitations

  * **Win32 DNS lookup for arbitrary-key signed-only scripts:**
    the Win32 platform does not implement `xDnsResolve`.
    Scripts signed with the embedded `keyRoot`, `key0`, or
    (under `ENABLE_TEST_KEY=1`) the test key work without DNS;
    scripts signed with arbitrary user keys whose public key
    must be fetched from a `pk_<token>` host require either a
    manually preloaded key or running on POSIX.
  * **R-marker coverage gap:** 32 of 1140 specification
    markers (2.81%) are not pinned by an existing test.
    The 32 fall into infrastructure-blocked groups
    (coding-guideline audit enforced by `audit_patterns.tcl`,
    signed-script fixtures, Win32-only callbacks,
    internal-only APIs, signal-driven paths).
  * **Defensive-macro (`NEVER`/`ALWAYS`) audit -- reviewed
    complete, no action required.**  Formerly tracked as the
    "Bug 26 family": a concern that under the
    `TH8_OMIT_AUXILIARY_SAFETY_CHECKS` build (an MC/DC
    instrumentation flag, not the default or debug build) the
    `NEVER`/`ALWAYS` macros collapse to `0`/`1` and could expose
    an unguarded NULL dereference.  The full per-site review
    (all 154 live macro sites, 2026-07-09) found **zero sites
    requiring conversion**: caller-supplied pointers that can
    legitimately be NULL are guarded with plain
    `if (!ptr) return TH8_ERROR;`, and the defensive macros wrap
    only genuine invariants.  Verified empirically by a full
    suite run of the `TH8_OMIT_AUXILIARY_SAFETY_CHECKS` build
    with zero crashes.  Listed here only for transparency;
    there is no user-facing defect and no remaining work.
  * **ISO Tcl standardisation track:** running in parallel as
    post-1.0 work; the 1.0.0 release is the pragmatic code
    release per the maintainer's release plan.  See
    `tcl_language_standard_v1.md` for the working text.

## Deferred for post-1.0

  * `switch -nocase`, `string cat`, `in` / `ni` `expr`
    operators, `catch` 3-argument options dict,
    `namespace ensemble/path/origin/which`,
    `lsearch -bisect/-subindices/-stride`,
    `lsort -indices/-stride`, `info hostname`.
  * Per-platform MC/DC coverage merge (requires CI
    infrastructure -- release-plan phase F1).
  * iOS and Android device-emulator CI test harnesses.
  * Closure of the 32 infrastructure-blocked R-marker gaps.

## Compatibility notes

  * TH8 1.0.0 commits to the **`TH8_STUBS_VERSION = 2`** ABI.
    Future binary-compatible additions go through
    `Th8_Platform` `nVersion` bumps with the merge-callback
    contract documented in `th8.h`.
  * TH8 is binary-compatible with `libth8stub.a`-linked
    extensions via `Th8_InitStubs`; downstream embedders that
    call only public `TH8_API` symbols are insulated from
    internal refactorings.
  * The `Th8_Set/GetPreGetDataCallback` APIs that appeared in
    pre-1.0 documentation drafts have been removed before this
    release; use `Th8_SetPolicyCallback` /
    `Th8_GetPolicyCallback` with the phase bitmask
    (`TH8_PHASE_PRE` / `POST` x `READ` / `EVAL`) instead.

## Acknowledgments

TH8 inherits the discipline of the SQLite test suite (every
requirement covered, every commit audited), the
single-binary-everywhere model of Cosmopolitan, the
clean-room embeddable shape of JimTcl, the Eagle security
architecture (Spilornis, Harpy, ConvertUTF_v2, structured
return codes, cancellation model), and the Tcl 8.6 language
design.

For the per-commit change history see `CHANGELOG.md` in the
repository root.

---

Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
See the file `license.terms` for usage and redistribution
terms and for a DISCLAIMER OF ALL WARRANTIES.
