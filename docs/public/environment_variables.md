# TH8 Environment Variables

Complete reference for the environment variables the TH8 shell (`th8sh`) and
library read at run time, grouped by purpose. Descriptions are taken from the
actual usage sites in the source.

Unless stated otherwise, only a variable's **existence** matters (its value is
ignored) and the described behavior is what you get when it is **set**; the
default is the unset behavior. Variables whose value *is* used say so
explicitly.

> **Security note.** The variables under *Security controls* **disable
> hardening**. They exist for testing, debugging, and tightly-controlled
> embedding — do not set them in a production or multi-tenant deployment.

---

## Security controls (leave unset in production)

### `TH8SH_NO_SCRIPT_SECURITY`
Crypto builds only. When **unset**, the shell installs the **signed-only script
policy** (`Th8_EnableSignedPolicy`), so `[source]` / `[load]` require a valid
`.b64sig` signature. When **set**, that policy is not installed and **unsigned,
unverified scripts run**. (If the policy is requested but fails to install, the
shell exits with a security error — that path is not reachable when this
variable is set.)

### `TH8SH_NO_PLATFORM_SECURITY`
When **set**, the shell skips the OpenBSD **`pledge(2)` / `unveil(2)`** sandbox
it otherwise applies at startup (`Th8Shell_ApplyPledgeUnveil`: `unveil` of `.`
as `rx`, `/dev/urandom` and `/dev/tty` as `r`, plus the corresponding
`pledge` promise set). No effect on platforms without pledge/unveil.

### `TH8_DNS_ROOT_KEY` — DNSSEC trust-anchor override (value = a file path)
Points `clock ntp`'s DNSSEC validator (libunbound builds) at an alternate root
trust-anchor file instead of the bundled one. **The file MUST carry a valid TH8
signature in a companion `<path>.b64sig`, which TH8 verifies against its
compiled-in signing key BEFORE the anchor is used.** A file an environment
variable can point at is attacker-influenceable, so it is pinned to TH8's key
rather than to filesystem permissions.

- Sign a custom anchor like any other TH8 artifact
  (`tools/signScript.sh <path>`); regenerate the bundled one with
  `tools/fetch_root_anchor.sh`.
- A missing or invalid signature => the override is **ignored** and resolution
  falls through to the insecure path, where `clock ntp`'s require-secure check
  **fails closed** rather than trusting an unvalidated answer.
- OS-managed **system** anchors (`/etc/unbound/root.key`,
  `/usr/share/dns/root.key`, …) are trusted as-is (already protected by the OS
  permission model); the `.b64sig` requirement applies only to the TH8-domain
  anchors — this variable and the bundled `root.key`.

---

## Feature toggles (shell startup)

Each of these **skips** enabling an optional capability the shell otherwise
turns on. (They are also summarized in the header comment of the shell's
capability-enabling routine.)

| Variable | Effect when set | Build gate |
|---|---|---|
| `TH8SH_NO_LOAD` | Skip `Th8_EnableLoad` — the `[load]` command is not enabled. | `TH8_ENABLE_LOAD` |
| `TH8SH_NO_UNLOAD` | Skip `Th8_EnableUnload` — `[unload]` is not enabled (otherwise enabled with `TH8_UNLOAD_OK | TH8_UNLOAD_DANGEROUS`). | `TH8_ENABLE_LOAD` |
| `TH8SH_NO_BIGINT` | Skip `Th8_EnableBigint` — arbitrary-precision integers are not auto-enabled. | `TH8_ENABLE_BIGINT` |
| `TH8SH_NO_SCRIPT_LIBRARY` | Do not source the startup script library (`lib/th8/init.th8`). | — |
| `TH8SH_NO_EXPR_FEATURES` | Do not apply any `[expr]` feature flags at startup (overrides `TH8SH_EXPR_FEATURES`). | — |
| `TH8SH_EXPR_FEATURES` | **Value** is a list of `[expr]` feature names, parsed and applied at startup (only when `TH8SH_NO_EXPR_FEATURES` is unset). | — |

---

## Interactive / signal handling

| Variable | Effect when set |
|---|---|
| `TH8SH_NO_CTRLC_HANDLER` | Do not install the interactive interrupt handler (Win32 `SetConsoleCtrlHandler`; POSIX `SIGINT` handler). |
| `TH8SH_NO_FATAL_HANDLER` | Do not install the crash handler (Win32 unhandled-exception filter; POSIX fatal-signal handlers). |
| `TH8SH_PLEASE_BE_QUIET` | Suppress the startup banner (`Th8Shell_PrintBanner` returns early). |
| `TH8SH_BREAK` | At the very start of `main()`, pause so a debugger can attach: on a TTY, print `attach debugger to PID N` and wait for ENTER (typing `exit` aborts startup); otherwise raise `SIGTRAP` (POSIX) / `DebugBreak()` (Win32). Read via raw `getenv` before any TH8 init. |

---

## Time & networking

| Variable | Effect |
|---|---|
| `TH8_DNS_ROOT_KEY` | DNSSEC trust-anchor override — see *Security controls* above. |
| `TH8_FORCE_HTTPS_TIME` | Force the HTTPS time source instead of NTP for authenticated wall-clock time. |

In libunbound builds, `clock ntp` performs **end-to-end DNSSEC-validated**
resolution: it resolves the server's A and AAAA records through the validating
resolver, requires a cryptographically *secure* answer, and connects only to
validated addresses (see §25.5 of the language extensions).

---

## Storage & per-user paths

| Variable | Effect |
|---|---|
| `TH8_SQLITE_DB` | **Value** is the SQLite database file path used by the `sqlite3` extension. |
| `XDG_DATA_HOME` | (POSIX) Base for the per-user **managed** DNSSEC anchor copy `$XDG_DATA_HOME/th8/root.key` (RFC 5011 auto-roll); falls back to `$HOME/.local/share`. |
| `HOME`, `USER` | (POSIX) Standard home-directory / user lookup used when composing per-user paths. |
| `APPDATA`, `USERPROFILE` | (Win32) Per-user base for the managed anchor copy. |

---

## Debug / development

| Variable | Effect |
|---|---|
| `TH8_HEAP_CHECK_EVERY` | (`TH8_MEM_DEBUG` builds) **Value** is the number of allocations between heap-integrity checks; `0` (or unset) disables periodic checking. |

---

## Testing (not for normal use)

Consumed by the test harness (`lib/Standard1.0/test.tcl`) and internal fixtures;
no effect on a normal deployment.

| Variable | Effect |
|---|---|
| `TH8SH_YES_TESTLIB` | Load the internal test library (`::th8testlib`). |
| `TH8_TEST_MATCH` / `TH8_TEST_SKIP` | **Value** is whitespace-separated glob patterns selecting / excluding test **names** (tcltest `-match` / `-skip`). |
| `TH8_TEST_FILE` / `TH8_TEST_NOTFILE` | **Value** is whitespace-separated glob patterns selecting / excluding test **files** (tcltest `-file` / `-notFile`). |
| `TH8TEST_COV_XYZ`, `TH8TEST_KV_TMP` | Internal test fixtures. |
