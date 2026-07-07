# TH8 Security Policy

TH8 is built around the assumption that the script *will* be hostile.
Security issues against the interpreter are taken seriously and
handled out-of-band before any public disclosure.

If you read [`docs/public/security_model.md`](docs/public/security_model.md)
first you will know what TH8 considers a security boundary; anything
that crosses one of those boundaries is the subject of this policy.

---

## How to report

**Do not open a public GitHub issue.**  Email instead:

> **security@mistachkin.com**

Include:

  * The shortest reproducer you can put together (a `.tcl` script
    plus the command to run it, ideally).
  * The TH8 version (`./bin/th8sh -version` output, or git commit
    hash).
  * The platform (`uname -a` or Windows build).
  * What you believe the security impact is (remote code execution,
    sandbox escape, information leak, denial of service, etc.) and
    why.

If you would like to encrypt your report, request a current PGP key
in the first message and we will reply with one.

You should hear back within **three business days**.  If you do not,
please email again; mail filters sometimes eat well-intentioned
security mail.

---

## Scope

In scope:

  * Anything that lets a script with no registered plugins escape
    the default zero-capability sandbox.
  * Anything that lets a signed-only-mode interpreter evaluate
    unsigned (or wrongly-signed) script.
  * Anything in the core (`src/th8_*.c`) that is exploitable from
    well-formed Tcl input.
  * Stack/heap memory corruption reachable from script input.
  * Cryptographic weakness in the Harpy signing pipeline.
  * Fault injection paths (`Th8_FaultConfig`) reachable in
    production builds without `TH8_ENABLE_FAULT_INJECTION`.

Out of scope (these are not security issues, though we still want
to know about them):

  * Resource exhaustion via *intentionally* runaway scripts inside
    a sandbox that has not been configured with quotas (the embedder
    is expected to configure quotas; the API for that is in the man
    page).
  * Bugs in vendored externals (mimalloc, tommath, regex,
    Cosmopolitan, etc.).  Report those upstream first; if TH8 is
    affected, we will mirror the upstream advisory.
  * Findings in `make mcdc-report` showing uncovered conditions --
    those are *engineering* gaps, not security ones, and live in
    the bug ledger rather than this policy.

---

## Disclosure process

  1. You email security@mistachkin.com.
  2. We confirm receipt within three business days and engage on
     the technical details.
  3. We work with you to confirm the issue and to scope a fix.
  4. We agree on a coordinated disclosure date.  Default window is
     **90 days** from the date of report, accelerated by mutual
     agreement if the fix is small and ready sooner, or extended by
     mutual agreement if the issue is structural and the fix needs
     time.
  5. We publish a fix, a release, and an advisory.  You are
     credited as the reporter (or not, at your preference).

We do not currently run a paid bug bounty programme.

---

## Distribution-time integrity

TH8 release artefacts (source tarball, `th8sh.com` Cosmopolitan
binary, signed amalgamation) are signed with the TH8 release key.
Verify the signature on every release artefact before using it in
production.  The release key fingerprint, the signature format, and
the verification steps are documented in
[`docs/public/RELEASE_NOTES.md`](docs/public/RELEASE_NOTES.md).

Production embedders should pin a specific release tag plus its
signature, not track `main`.

---

Thanks for taking the time.  Coordinated disclosure helps everyone
who runs TH8 in production stay safe.
