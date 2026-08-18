#!/usr/bin/env bash
#
# fetch_root_anchor.sh --
#
#     Regenerate the bundled IANA DNS root trust anchor (tools/data/root.key)
#     and (re)sign it, producing tools/data/root.key.b64sig.
#
#     The anchor is the root zone KSK DNSKEY set.  It is the DNS analogue of a
#     root CA bundle: public, stable (the KSK rolls only every several years),
#     and redistributed by design (data.iana.org/root-anchors/).  TH8 seeds its
#     RFC 5011 auto-roll managed copy from this file, so once bootstrapped the
#     working copy tracks future rollovers on its own -- this file only needs a
#     refresh when ICANN publishes a new KSK.
#
# Provenance:
#     The anchor is produced by `unbound-anchor`, the canonical NLnet Labs tool,
#     which fetches the live root DNSKEY and VALIDATES it against its built-in
#     ICANN trust anchor and the p7s-signed root-anchors.xml before writing it.
#     We then strip unbound's volatile autotrust state (timestamps) to keep the
#     committed file stable, emitting only the DNSKEY RRs + a provenance header.
#
# Integrity:
#     The bundled root.key ships next to the binary, a softer tamper target
#     than the privileged system anchor paths, so it is TH8-signed: this script
#     writes root.key.b64sig alongside it, and the loader verifies that
#     signature (against the compiled-in key) BEFORE handing the anchor to
#     libunbound.  A custom anchor pointed to by $TH8_DNS_ROOT_KEY must be
#     signed the same way.
#
# Usage:
#     bash tools/fetch_root_anchor.sh [th8sh]
#
#     th8sh defaults to ./bin/th8sh (used to sign; TH8SH_YES_TESTLIB=1 signs
#     with the local test key -- re-sign with the production key before commit).
#
set -euo pipefail

here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$here/.." && pwd)
out="$root/tools/data/root.key"
th8sh="${1:-$root/bin/th8sh}"

ua=$(command -v unbound-anchor || echo /opt/homebrew/sbin/unbound-anchor)
if [ ! -x "$ua" ]; then
    echo "fetch_root_anchor: unbound-anchor not found (install unbound)" >&2
    exit 1
fi

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

# 0 = anchor already current, 1 = anchor updated; both are success.
set +e
"$ua" -a "$tmp" -v
rc=$?
set -e
if [ "$rc" -gt 1 ] || [ ! -s "$tmp" ]; then
    echo "fetch_root_anchor: unbound-anchor failed (rc=$rc)" >&2
    exit 1
fi

# Emit a stable, DNSKEY-only anchor (drop unbound's ;;autotrust state so the
# committed file does not churn on every regeneration).  Keep each DNSKEY RR
# and annotate it with its key tag.
{
    echo "; TH8 bundled IANA DNS root trust anchor (root zone KSK DNSKEY set)."
    echo "; Generated + validated by unbound-anchor; regenerate with"
    echo ";   bash tools/fetch_root_anchor.sh"
    echo "; DO NOT hand-edit.  See tools/fetch_root_anchor.sh for provenance."
    awk '
        /IN[ \t]+DNSKEY[ \t]+257/ {
            tag = "?"
            if (match($0, /id = [0-9]+/)) tag = substr($0, RSTART + 5, RLENGTH - 5)
            sub(/[ \t]*;.*$/, "")        # strip trailing ;{...} ;;state comments
            sub(/[ \t]+$/, "")
            printf "%s  ; KSK key tag %s\n", $0, tag
        }
    ' "$tmp"
} > "$out"

nkeys=$(grep -c 'IN[[:space:]]*DNSKEY' "$out" || true)
if [ "${nkeys:-0}" -lt 1 ]; then
    echo "fetch_root_anchor: no DNSKEY records extracted" >&2
    exit 1
fi
echo "fetch_root_anchor: wrote $out ($nkeys KSK record(s))"

if [ -x "$th8sh" ]; then
    TH8SH_YES_TESTLIB=1 bash "$root/tools/signScript.sh" "$out" "$th8sh"
    echo "fetch_root_anchor: signed -> $out.b64sig"
    echo "fetch_root_anchor: NOTE re-sign with the production key before commit."
else
    echo "fetch_root_anchor: WARNING th8sh '$th8sh' not executable; $out is UNSIGNED." >&2
    echo "fetch_root_anchor: run tools/signScript.sh on it before use." >&2
fi
