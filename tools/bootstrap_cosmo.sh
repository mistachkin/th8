#!/bin/bash
###############################################################################
#
# bootstrap_cosmo.sh --
#
#     One-command bootstrap for the Cosmopolitan Libc toolchain so
#     `make -f Makefile.cosmopolitan` can build a single-binary
#     Actually Portable Executable (APE) of th8sh that runs on
#     Linux, macOS, Windows, FreeBSD, OpenBSD, and NetBSD.
#
#     Mirrors the pattern of tools/regex_vendor.tcl / tools/vendor_tcl.tcl
#     (vendor-managed external):
#
#       - The pinned cosmocc release is checked into the source tree
#         under externals/cosmopolitan/.  After a fresh clone the
#         toolchain is already present; running this script is a
#         no-op fast path.
#       - When the toolchain is missing or the user passes --force,
#         this script re-runs the upstream
#         externals/cosmopolitan/build/download-cosmocc.sh with the
#         pinned version + SHA-256 from COSMO_VERSION.
#
# Usage:
#
#     bash tools/bootstrap_cosmo.sh                # bootstrap if missing
#     bash tools/bootstrap_cosmo.sh --force        # redownload toolchain
#     bash tools/bootstrap_cosmo.sh --check        # verify only; exit nonzero
#                                                  # if the toolchain is
#                                                  # missing or broken
#
# Pinning:
#
#     The pinned version + checksum live in COSMO_VERSION at the repo
#     root (one shell-style line per field).  Update by editing
#     COSMO_VERSION and re-running this script with --force.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
COSMO_DIR="$REPO_ROOT/externals/cosmopolitan"
COSMOCC_DIR="$COSMO_DIR/tool/cosmocc"
COSMOCC_BIN="$COSMOCC_DIR/bin/cosmocc"
PIN_FILE="$REPO_ROOT/COSMO_VERSION"
DOWNLOADER="$COSMO_DIR/build/download-cosmocc.sh"

MODE=bootstrap
case "${1:-}" in
    --force)  MODE=force ;;
    --check)  MODE=check ;;
    "")       MODE=bootstrap ;;
    -h|--help)
        sed -n '/^# Usage:/,/^# Pinning:/p' "$0" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "$0: unknown argument: $1" >&2
        echo "Try: $0 --help" >&2
        exit 2
        ;;
esac

# ----------------------------------------------------------------------
# Probe: is the toolchain already installed and runnable?
# ----------------------------------------------------------------------

probe_ok() {
    [ -x "$COSMOCC_BIN" ] || return 1
    "$COSMOCC_BIN" --version >/dev/null 2>&1 || return 1
    return 0
}

if [ "$MODE" = "check" ]; then
    if probe_ok; then
        echo "cosmocc: OK ($("$COSMOCC_BIN" --version 2>&1 | head -1))"
        exit 0
    fi
    echo "cosmocc: MISSING or broken at $COSMOCC_BIN" >&2
    exit 1
fi

if [ "$MODE" = "bootstrap" ] && probe_ok; then
    echo "cosmocc already installed ($("$COSMOCC_BIN" --version 2>&1 | head -1))"
    echo "Re-run with --force to redownload."
    exit 0
fi

# ----------------------------------------------------------------------
# Bootstrap path: invoke the upstream download script.
# ----------------------------------------------------------------------

if [ ! -f "$PIN_FILE" ]; then
    cat >&2 <<EOF
$0: fatal: COSMO_VERSION pin file not found at $PIN_FILE.

The pin file must define the toolchain version and SHA-256 checksum,
e.g.:

    COSMOCC_VERSION=4.0.2
    COSMOCC_SHA256=<sha256-of-cosmocc-4.0.2.zip>

The values are documented at
https://github.com/jart/cosmopolitan/releases.
EOF
    exit 1
fi

if [ ! -x "$DOWNLOADER" ]; then
    echo "$0: fatal: upstream downloader not found at $DOWNLOADER" >&2
    echo "Run a fresh clone to restore externals/cosmopolitan/build/." >&2
    exit 1
fi

# shellcheck disable=SC1090
. "$PIN_FILE"

if [ -z "${COSMOCC_VERSION:-}" ] || [ -z "${COSMOCC_SHA256:-}" ]; then
    echo "$0: fatal: COSMO_VERSION must define COSMOCC_VERSION and COSMOCC_SHA256" >&2
    exit 1
fi

if [ "$MODE" = "force" ] && [ -d "$COSMOCC_DIR" ]; then
    echo "Removing existing $COSMOCC_DIR ..."
    rm -rf "$COSMOCC_DIR"
fi

echo "Downloading cosmocc $COSMOCC_VERSION into $COSMOCC_DIR ..."
"$DOWNLOADER" "$COSMOCC_DIR" "$COSMOCC_VERSION" "$COSMOCC_SHA256"

if probe_ok; then
    echo "cosmocc installed: $("$COSMOCC_BIN" --version 2>&1 | head -1)"
    echo
    echo "Next:  make -f Makefile.cosmopolitan fresh"
else
    echo "$0: post-install probe failed; manual investigation needed" >&2
    exit 1
fi
