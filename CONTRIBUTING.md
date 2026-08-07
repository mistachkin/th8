# Contributing to TH8

Thanks for taking a look.  This guide tells you how to build, how to
test, what conventions the codebase expects, and how to submit a
change that has a chance of being merged.

If you are an **AI agent** working on this codebase, also read
[`CLAUDE.md`](CLAUDE.md) in the repository root.  It is the canonical
agent guide and covers the standing rules, the audit gate, the
R-marker discipline, and the test-signing workflow in much more
detail than this file does.

## The bar

Before you read the workflow, please read the bar.  It is the same
for every contributor and every tool:

  * **No bug is too small.**  A wrong comment, a stray blank line,
    an unchecked return code in a path the compiler "can never
    reach," an off-by-one in a documentation example -- all of
    these are real issues, get real fixes, and are appropriate
    subjects for a pull request.
  * **No detail of formatting is minor enough to ignore.**  Code
    that does not pass `make audit-format` does not merge.  Tests
    that do not follow the prologue / epilogue / R-marker
    convention do not merge.  Documentation that drifts out of
    sync with code does not merge.
  * **Code is meant to be beautiful.**  Easy to read, easy to
    audit, documented function-by-function, tested by purpose.
    "It works on my machine" is not the standard; "the next reader
    can pick it up cold and understand it" is.
  * **Full test coverage is the baseline.**  MC/DC coverage is the
    target.  Every meaningful new condition gets a test.
    Intentional coverage exemptions (Win32-only on a POSIX run, a
    fault path with no fault facility yet, a verified-intrinsic-
    dead arm) get written down with their justification.
  * **Security posture improves continuously.**  TH8 is intended to
    be safe to run as a trusted sandbox for untrusted or
    machine-generated input, including agentic collaboration
    contexts.  Every release should narrow the trusted computing
    base, not widen it.  Changes that broaden attack surface
    without a commensurate security argument are unlikely to land.

If you are unsure whether a small change is "worth" submitting:
yes, it is.  The project values the small fixes.

---

## TL;DR for human contributors

1. **Build** with `make fresh` (release).  Hack with
   `make ENABLE_TEST_KEY=1 clean debug` (debug, embeds the test
   signing key so the test suite does not have to fetch the
   production key).
2. **Test** with `./bin/th8sh tests/all.tcl`.  Every test must pass
   before any change is considered complete.
3. **Audit** with `make audit` (banned patterns, CRT object scan,
   format check).  Promote pass/fail of `make audit` to your local
   pre-commit hook.
4. **Sign** any signed test files you touched with
   `tools/signScript.sh tests/<file>.tcl`.  Do **not** mass-sign
   `tests/*.tcl`.
5. **Submit** via pull request against `main`.  The pull request
   description should reference the requirement (R-marker) or bug
   number the change addresses, and include a one-line summary of
   the test coverage you added.

The remainder of this document covers each of the above in detail
and explains the project's conventions.

---

## 1. Building

### Clone

```
git clone --recurse-submodules https://github.com/<owner>/th8.git
cd th8
```

If you forgot `--recurse-submodules`:

```
git submodule update --init --recursive
```

The submodules today are `externals/mimalloc/vendor` (Microsoft
mimalloc, the production allocator) and `externals/tommath/vendor`
(libtom/libtommath, the source for the bignum amalgamation built
under `externals/tommath/build/` by `make tommath_vendor`).

### Required toolchain

- A C99 compiler (clang or gcc).  clang is the reference; it is also
  what `make mcdc` requires.
- GNU make.
- `tclsh` >= 8.6 (the `audit-format` gate inside `make audit` runs
  `tools/format_code.tcl`, which `package require Tcl 8.6`).
- `clang-format-19` (the `audit-format` gate enforces only this
  canonical major version; other versions warn-and-skip).  `make
  pkg-deps-core` installs it on Debian/Ubuntu; see the Portability
  Guide (`docs/public/portability.md`) for the macOS install steps.
- `pkg-config`, OpenSSL + libcurl + libunbound development headers.
- For Cosmopolitan builds: nothing extra; run `bash
  tools/bootstrap_cosmo.sh` once and `make -f Makefile.cosmopolitan`
  picks it up.
- For Windows: MSVC `cl.exe` + `nmake` plus the Win32 third-party
  libraries documented in `externals/<name>/README.md`.

### Build targets

| Target                     | Effect                                                  |
|----------------------------|---------------------------------------------------------|
| `make fresh`               | clean release build + audit + `tests/all.tcl`           |
| `make`                     | incremental build                                       |
| `make ENABLE_TEST_KEY=1 clean debug` | debug build with the test signing key embedded |
| `make audit`               | banned-pattern scan + format check + CRT scan           |
| `make mcdc`                | MC/DC-instrumented build + suite run                    |
| `make mcdc-report`         | per-file MC/DC summary from the last `make mcdc` run    |
| `make mcdc-uncovered`      | list of uncovered MC/DC conditions                      |
| `make manlint`             | mandoc lint on `docs/public/*.1` and `*.3`               |
| `make -f Makefile.cosmopolitan` | Cosmopolitan APE build (`bin-cosmo/th8sh.com`)     |
| `nmake /f Makefile.msc`    | Windows MSVC build                                      |

`make fresh` is what CI runs.  If `make fresh` does not succeed on
your machine before your change, do not submit the pull request.

---

## 2. Testing

### Running the suite

```
./bin/th8sh tests/all.tcl
```

The suite is roughly 4,200 cases as of 1.0.0 and runs in
single-digit-minutes on a modern laptop.  Failures are reported in
the form `++++ <name> FAILED`; zero failures is the only acceptable
result before merge.

### Running a subset

Four environment variables select a subset of the suite without
editing `tests/all.tcl`.  They are the env-driven equivalents of
tcltest's `configure -match / -skip / -file / -notFile` options and
each holds a **space-separated list of `[string match]` glob
patterns**.  Files filter by their `all.tcl` name (e.g. `expr.tcl`);
tests filter by their `<group>-<seq>` name (e.g. `append-1.3`).

| Variable            | Selects                                    |
|---------------------|--------------------------------------------|
| `TH8_TEST_FILE`     | run only test files matching a pattern     |
| `TH8_TEST_NOTFILE`  | skip test files matching a pattern         |
| `TH8_TEST_MATCH`    | run only tests whose name matches          |
| `TH8_TEST_SKIP`     | skip tests whose name matches              |

A file (or test) runs when it matches an include pattern -- or the
corresponding include list is empty, meaning "all" -- and matches no
exclude pattern.  De-selected files and tests are simply not run and
not counted, so the summary reflects only the subset.

```
# just the expr and string files:
TH8_TEST_FILE="expr*.tcl string*.tcl" ./bin/th8sh tests/all.tcl

# everything except the (slow) coverage files:
TH8_TEST_NOTFILE="coverage_*.tcl" ./bin/th8sh tests/all.tcl

# only the append-1.* tests within append.tcl:
TH8_TEST_FILE="append.tcl" TH8_TEST_MATCH="append-1.*" \
    ./bin/th8sh tests/all.tcl
```

This is the mechanism a critical-subset smoke run (e.g. under
Valgrind) uses to stay fast.  Under Tcl/Eagle the values come from the
`::env` array; under TH8, which does not auto-link `::env`, they are
read through the testlib's `env_kv` command, so a subset run must load
the testlib (the standard harness does this automatically).

### Adding tests

- New tests live alongside existing files in `tests/`.  Follow the
  `runTest { test <group>-<seq> {...} -constraints {...} -body
  {...} -result {...} }` convention.
- New test files must be added to `tests/all.tcl` alphabetically.
- Test-only C code goes in `src/test/th8_testlib.c`, not in
  production source.
- Every requirement that the test exercises should be referenced by
  its R-marker (`R-12345-67890`).  See section 4 below.

### Sign signed test files

A signed test file is one whose top of file declares
`-constraints {... signed_only ...}` or otherwise interacts with the
signed-only loader.  These files need a companion `.b64sig` so the
signed-only gate accepts them.

```
tools/signScript.sh tests/<file>.tcl
```

**Do not** run `tools/signScript.sh tests/*.tcl` over the whole
directory.  Re-sign only the files you actually changed; mass
re-signing makes review unreadable and obscures any unintended
change.

The default build uses the production signing key.  For hacking,
use `make ENABLE_TEST_KEY=1 clean debug` so the test key is embedded
and the suite does not have to fetch any external key material.

### Sign every documentation file you change

As a supply-chain integrity measure, **every documentation file
in the public release carries a companion `.b64sig`**.  That
includes `README.md`, `CONTRIBUTING.md`, `SECURITY.md`,
`CLAUDE.md`, the public docs under `docs/public/`, the internal
work-log docs under `docs/internal/`, the external-component
READMEs, the man pages, and even the working notes under
`docs/scratch/` and `docs/private/`.

When you edit a documentation file, re-sign it before committing:

```
tools/signScript.sh path/to/changed.md
```

The signature lets downstream consumers (and the project's own
audit gate) confirm that the documentation they are reading is
the same documentation that the author signed.  Touching a doc
without re-signing it leaves a known-stale signature behind,
which is worse than no signature at all -- treat the re-sign
as part of the edit, not a separate step.

---

## 3. Coding conventions

The C and Tcl style guides are the canonical references.  Skim them
before your first contribution:

- C source style: [`docs/public/c_source_code_style.md`](docs/public/c_source_code_style.md).
- Tcl / Eagle / TH8 script style:
  [`docs/public/tcl_eagle_th8_style_guide.md`](docs/public/tcl_eagle_th8_style_guide.md).

The rules an AI agent (or a new human contributor) is most likely
to trip over:

- **Every public function gets its own per-function header
  comment.**  No group-style shared headers.
- **Internal functions are named `th8Whatever`, not `Th8_Whatever`.**
  The latter is reserved for the public ABI.
- **Public functions declare with `TH8_API`; internal functions
  declare with `TH8_INTERNAL` in the header only.**  The `.c`
  definition uses plain `int` / `void`, no `static`.
- **Every function return value is checked for failure**, even if
  the call is "impossible" to fail.  Security-policy rule.
- **No bare `a * b` on `size_t` sizes.**  Use `TH8_SAFE_MUL_SIZE`
  or `TH8_ALLOC_MUL_ADD`.  Same applies even when the multiply is
  "safe in context."
- **R-markers must be MD5-derived** via `tools/mkreq.tcl`, never
  random.  See section 4.
- **Test results are gospel.**  If a test fails after your change,
  fix the code; do not edit the expected result.
- **`make audit` must remain green.**  The banned-pattern scan
  catches a substantial set of common errors at commit time.

### Compound conditions and MC/DC

TH8 takes MC/DC coverage seriously.  When you write a compound
condition (`A && B`, `A || B || C`), be aware that clang's MC/DC
encoder has a representation limit -- above ~6 conditions or with
certain nesting shapes, the decision is silently omitted from the
coverage map.

[`docs/internal/FINDINGS.md`](docs/internal/FINDINGS.md) Finding 005
documents the discipline (this document is excluded from the public
release; ask for a copy if you are working on coverage).  In
short: if the compound's largest arm is intrinsic-dead (cannot fire
in the test corpus), prefer nested singles over a 4+ condition
chain.

`make mcdc-report` shows you where you stand.  Aim to keep
lib-wide MC/DC moving forward.  A change that drops MC/DC without
documenting why will be asked to add tests or refactor.

---

## 4. R-markers and the requirement-test linkage

TH8 has a written language standard with each behaviour tagged by
an `R-NNNNN-NNNNN` requirement marker.  Every test references the
marker(s) it exercises.

- New requirements: add them to the language standard
  (`docs/public/tcl_language_standard_v1.md`), then run
  `tools/mkreq.tcl --verify` to compute the canonical R-markers.
- New tests: include the R-marker(s) in the test description so
  `tools/mkreq.tcl --check-tests` can confirm coverage.
- Edits to existing requirement text MUST be followed by re-running
  `tools/mkreq.tcl` so the R-marker recomputes, and by updating
  every test that referenced the old marker.

The `make audit-reqs` target runs the full requirement-vs-test
linkage check.

---

## 5. Bugs, findings, and the "incomplete" ledger

Two internal documents (not shipped publicly; available to active
contributors on request) track the state of the world:

- `docs/internal/incomplete.md` -- numbered live bug ledger and
  TODO list.  Every observed bug, every deferred TODO, every
  follow-up gets a numbered entry here with a reproducer.
- `docs/internal/FINDINGS.md` -- per-iteration engineering analysis.
  Each entry is dated and numbered and explains *why* a decision
  was made, not just what was done.

If you find a bug while implementing something else, **log it; do
not silently route around it.**  The repository convention is "fix it
now, or note it persistently" -- never quietly skip.

If you find unexpected behaviour during coverage or fuzz work, log
it as a new numbered Bug entry with a reproducer in
`incomplete.md` *before* you reroute.  Future iterations will
forget the context if you do not.

---

## 6. Submitting a pull request

A good pull request for TH8 has:

1. A title that names the change ("`format`: fix `%g`
   round-half-down" not "fixes").
2. A description that links the requirement (R-marker) or bug
   number the change addresses.
3. A test that exercises the change.  Add it to the right file in
   `tests/`, with the R-marker referenced.
4. `make fresh && ./bin/th8sh tests/all.tcl` passing locally.
5. `make audit` passing locally.
6. If your change touches MC/DC-instrumented code, `make
   mcdc-report` should not regress.

Branch from `main`, open the pull request against `main`.  Continuous
integration
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) will rerun
the full matrix on every push.

For substantive design changes (new language feature, new platform
backend, new fault type), open an issue first so we can discuss the
shape before you write the code.

---

## 7. AI-tool-assisted contributions

A large portion of TH8 was authored in collaboration with Claude.
Pull requests written, drafted, or assisted by **any AI tool you
prefer** (Claude, Copilot, Cursor, your own home-grown agent) are
welcome on exactly the same terms as fully human-authored ones.
Tool choice is not a gate; output quality is.  Specifically:

- The agent should follow [`CLAUDE.md`](CLAUDE.md) (or the
  equivalent standing-rules document for whichever tool you are
  using).  `CLAUDE.md` is also the densest single-document tour of
  the codebase for a new human contributor; it is worth a read
  either way.
- The human in the loop reviewed each change before pushing.  AI
  tools make confident mistakes; you are responsible for catching
  them before the pull request lands in the review queue.
- The pull request description says so plainly (a
  `Co-authored-by:` line or a one-sentence note in the
  description).  We do not mind; we just want to know.

The standing rules in `CLAUDE.md` exist precisely so that
agent-authored work can be reviewed quickly and merged with
confidence.  If you have an agent that wants to contribute, point
it at `CLAUDE.md` first.

---

## 8. Proposing new features

New features are welcome.  We do ask that they meet the same bar
as the rest of the project, which in concrete terms means:

  * **Follow the relevant industry standard verbatim.**  If you
    propose an XSLT 2.0 parser, it follows the W3C XSLT 2.0
    specification.  If you propose a new TLS option, it follows
    the relevant IETF RFC.  If you propose a new language
    construct, it follows the Tcl language standard or extends it
    explicitly via an R-marker.  We do not accept "approximately
    follows" -- if your feature is in a domain that has a written
    standard, that standard is the contract.
  * **Tests, before the feature is considered done.**  A new
    feature without a test suite is a deferred liability.  We do
    not merge those.
  * **Documentation, matching the project's style.**  If the
    feature touches the language, it goes in the language standard
    with R-markers and conformance tests against both TH8 and
    reference Tcl where the semantics overlap.  If it touches the
    C ABI, it goes in `docs/public/th8_api.3` and the
    `docs/public/th8_language_extensions.md` (where appropriate),
    with the same level of detail as the rest of the man page.
  * **An issue first, for anything substantive.**  Before you
    write code for a feature that is more than a hundred lines
    or that touches a new platform / new subsystem, open an issue
    so the shape can be agreed.  It is much easier to redirect a
    design at the issue stage than at the pull request stage.

---

## 9. Proposing changes to the Tcl Language Standard

[`docs/public/tcl_language_standard_v1.md`](docs/public/tcl_language_standard_v1.md)
is the working text against which the TH8 conformance suite is
tested.  The intent is to submit this document to ISO once it has
reached the required maturity.  Until then, **proposals for
clarifications, additional R-markers, edge-case wording, and
precision improvements are first-class contributions.**

To propose a change to the standard:

  1. Open a pull request against the standard text itself.
  2. In the description, name the section and the specific
     ambiguity / gap / wording your change addresses.
  3. If your proposed wording adds or changes a normative
     statement, add (or update) the corresponding R-marker via
     `tools/mkreq.tcl` and add a conformance test that exercises
     the new wording.
  4. Cross-engine validity matters: where TH8 follows Tcl 8.6,
     the test should pass under both engines.  Where TH8
     intentionally diverges (documented in
     `docs/public/th8_language_extensions.md`), say so.

We want the standard to be the best possible specification of the
Tcl language by the time it is submitted to ISO; rigorous proposals
help get it there.

---

## 10. Security issues

Security issues should be reported per [`SECURITY.md`](SECURITY.md),
not as a public GitHub issue.  Please do not disclose vulnerabilities
in a pull request description until they have been addressed.

---

Thanks again for your interest.  See you in the issue tracker.
