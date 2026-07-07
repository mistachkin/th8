#!/bin/bash
###############################################################################
#
# bootstrap_win32_deps.sh --
#
#     One-command bootstrap of the Win32 third-party libraries
#     consumed by Makefile.msc:
#
#         externals/openssl/{include,lib}/   -- OpenSSL 3.x
#         externals/zlib/{include,lib}/      -- zlib
#         externals/curl/{include,lib}/      -- libcurl (static)
#         externals/tcl/{include,lib}/       -- Tcl 8.6 (headers + stub)
#
#     The build infrastructure (per-external README.md and any
#     build_win{32,64}.bat files) is checked in to the repository.  The
#     SOURCE for each library is fetched on demand and the BUILT
#     output is generated locally; none of those bytes are
#     source-controlled.
#
#     This script automates the fetch step on POSIX (macOS / Linux /
#     WSL) using shasum-verified upstream tarballs.  On a native
#     Windows MSVC environment, run the per-external
#     build_win{32,64}.bat scripts directly after this script has
#     populated externals/<name>/src/.
#
# Usage:
#
#     bash tools/bootstrap_win32_deps.sh             # bootstrap all
#     bash tools/bootstrap_win32_deps.sh --check     # verify only
#     bash tools/bootstrap_win32_deps.sh openssl     # bootstrap one
#
# Pinning:
#
#     Each pinned version + SHA-256 is declared inline in this script
#     in the WIN32_DEPS_* environment block below.  Update by editing
#     this script and re-running.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

# ---------------------------------------------------------------------
# Pinned versions and checksums.
#
# Each block sets:
#   WIN32_DEPS_<NAME>_VERSION   upstream version string
#   WIN32_DEPS_<NAME>_URL       upstream tarball URL
#   WIN32_DEPS_<NAME>_SHA256    SHA-256 of the tarball
# ---------------------------------------------------------------------

# OpenSSL 3.x.  Upstream moved canonical downloads from
# www.openssl.org to GitHub Releases in 2024; the GitHub URL is
# the stable redirect target.
WIN32_DEPS_OPENSSL_VERSION=3.3.2
WIN32_DEPS_OPENSSL_URL_BASE="https://github.com/openssl/openssl/releases/download"
WIN32_DEPS_OPENSSL_URL="${WIN32_DEPS_OPENSSL_URL_BASE}/openssl-${WIN32_DEPS_OPENSSL_VERSION}/openssl-${WIN32_DEPS_OPENSSL_VERSION}.tar.gz"
WIN32_DEPS_OPENSSL_SHA256="2e8a40b01979afe8be0bbfb3de5dc1c6709fedb46d6c89c10da114ab5fc3d281"

# zlib.
WIN32_DEPS_ZLIB_VERSION=1.3.2
WIN32_DEPS_ZLIB_URL="https://zlib.net/zlib-${WIN32_DEPS_ZLIB_VERSION}.tar.gz"
WIN32_DEPS_ZLIB_SHA256="bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"

# libcurl (static, Schannel + libssh2 disabled to minimise surface).
WIN32_DEPS_CURL_VERSION=8.10.1
WIN32_DEPS_CURL_URL="https://curl.se/download/curl-${WIN32_DEPS_CURL_VERSION}.tar.gz"
WIN32_DEPS_CURL_SHA256="d15ebab765d793e2e96db090f0e172d127859d78ca6f6391d7eafecfd894bbc0"

# Tcl 8.6 (latest patch; needed only for the Win32 audit-format gate).
WIN32_DEPS_TCL_VERSION=8.6.14
WIN32_DEPS_TCL_URL="https://prdownloads.sourceforge.net/tcl/tcl${WIN32_DEPS_TCL_VERSION}-src.tar.gz"
WIN32_DEPS_TCL_SHA256="5880225babf7954c58d4fb0f5cf6279104ce1cd6aa9b71e9a6322540e1c4de66"

# libunbound (DNSSEC-validating recursive resolver; consumed by
# Th8_Platform.xDnsResolve on Win32 when ENABLE_UNBOUND=1).  Depends
# only on OpenSSL (already bootstrapped above) for the validator
# crypto.  libexpat is intentionally omitted: it is required by the
# `unbound-anchor` bootstrap tool (which downloads the initial
# root.key and verifies it against the ICANN CA), but NOT by the
# core resolver or by `ub_ctx_add_ta_autr` -- TH8 ships its own
# vetted root.key alongside the DLL and bootstraps the writable
# managed copy itself (see `th8Win32SetupManagedAnchor`), so the
# `unbound-anchor` workflow is not used and the expat dependency
# can be dropped.
#
# IMPORTANT (security): the SHA-256 below MUST be re-verified against
# NLnet Labs's signed checksum file
# (https://nlnetlabs.nl/downloads/unbound/unbound-${VERSION}.tar.gz.sha256)
# before any release that bundles this dependency.  Anyone bumping
# the pinned version must also update the SHA from the upstream
# .sha256 file and (ideally) verify the .asc PGP signature.  The
# sha256_check helper below fails closed on any mismatch, so an
# incorrect pin will surface immediately on first fetch.
WIN32_DEPS_UNBOUND_VERSION=1.20.0
WIN32_DEPS_UNBOUND_URL="https://nlnetlabs.nl/downloads/unbound/unbound-${WIN32_DEPS_UNBOUND_VERSION}.tar.gz"
WIN32_DEPS_UNBOUND_SHA256="56b4ceed33639522000fd96775576ddf8782bb3617610715d7f1e777c5ec1dbf"

# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------

log() { echo "[bootstrap_win32_deps] $*"; }
die() { echo "[bootstrap_win32_deps] ERROR: $*" >&2; exit 1; }

sha256_check() {
    # $1 = file, $2 = expected hex
    local file=$1
    local expected=$2
    local actual
    if command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "$file" | cut -d' ' -f1)
    elif command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$file" | cut -d' ' -f1)
    else
        die "neither shasum nor sha256sum is available; install one"
    fi
    if [ "$actual" != "$expected" ]; then
        die "checksum mismatch for $file: expected $expected, got $actual"
    fi
}

fetch_one() {
    # $1 = name (e.g. openssl), $2 = url, $3 = sha256
    local name=$1 url=$2 sha=$3
    local target_dir="$REPO_ROOT/externals/$name/src"
    local tarball="$target_dir/$(basename "$url")"

    mkdir -p "$target_dir"

    if [ -f "$tarball" ]; then
        log "$name: tarball already present at $tarball"
    else
        log "$name: fetching $url"
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 3 -o "$tarball" "$url"
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$tarball" "$url"
        else
            die "neither curl nor wget is available; install one"
        fi
    fi

    sha256_check "$tarball" "$sha"

    log "$name: extracting into $target_dir"
    (cd "$target_dir" && tar xf "$tarball")
}

# ---------------------------------------------------------------------
# Per-dependency bootstrap (fetch only -- the actual nmake build is
# left to externals/<name>/build_win64.bat or the user's MSVC
# environment).
# ---------------------------------------------------------------------

bootstrap_openssl() {
    fetch_one "openssl" "$WIN32_DEPS_OPENSSL_URL" "$WIN32_DEPS_OPENSSL_SHA256"
    log "openssl: now run on a Windows MSVC machine:"
    log "    cd externals\\openssl\\src\\openssl-${WIN32_DEPS_OPENSSL_VERSION}"
    log "    perl Configure VC-WIN64A no-shared no-asm --prefix=..\\.."
    log "    nmake && nmake install_sw"
}

bootstrap_zlib() {
    fetch_one "zlib" "$WIN32_DEPS_ZLIB_URL" "$WIN32_DEPS_ZLIB_SHA256"
    log "zlib: now run on a Windows MSVC machine:"
    log "    cd externals\\zlib\\src\\zlib-${WIN32_DEPS_ZLIB_VERSION}"
    log "    nmake -f win32\\Makefile.msc"
    log "    copy zlib.lib ..\\..\\lib\\x64\\"
    log "    copy zlib.h zconf.h ..\\..\\include\\"
}

bootstrap_curl() {
    fetch_one "curl" "$WIN32_DEPS_CURL_URL" "$WIN32_DEPS_CURL_SHA256"
    log "curl: see externals/curl/build_win64.bat for the next step"
}

bootstrap_tcl() {
    fetch_one "tcl" "$WIN32_DEPS_TCL_URL" "$WIN32_DEPS_TCL_SHA256"
    log "tcl: now run on a Windows MSVC machine:"
    log "    cd externals\\tcl\\src\\tcl${WIN32_DEPS_TCL_VERSION}\\win"
    log "    nmake -f makefile.vc INSTALLDIR=..\\..\\..\\.. install"
}

bootstrap_unbound() {
    fetch_one "unbound" "$WIN32_DEPS_UNBOUND_URL" "$WIN32_DEPS_UNBOUND_SHA256"
    log "unbound: now run on a Windows MSVC machine:"
    log "    cd externals\\unbound\\src\\unbound-${WIN32_DEPS_UNBOUND_VERSION}"
    log "    rem  Requires OpenSSL bootstrapped above; SSL_DIR points at it."
    log "    nmake /f makefile.vc8 SSL_DIR=..\\..\\..\\openssl"
    log "    rem  copy build outputs into externals\\unbound\\{include,lib}"
    log "    copy unbound.h ..\\..\\include\\"
    log "    copy libunbound.lib ..\\..\\lib\\x64\\unbound.lib"
    log ""
    log "unbound: trust anchor is NOT fetched here.  Embedders should"
    log "    bundle a current copy of the IANA root.key alongside the"
    log "    DLL; on the first DNS resolve, TH8 will copy it into"
    log "    %APPDATA%\\TH8\\root.key and switch to ub_ctx_add_ta_autr"
    log "    for RFC 5011 automatic key-rollover.  Alternatively, set"
    log "    TH8_DNS_ROOT_KEY to an absolute path to use it directly"
    log "    in static mode (no auto-roll)."
    log "    Download from:  https://www.iana.org/dnssec/files"
}

# ---------------------------------------------------------------------
# Check-only mode
# ---------------------------------------------------------------------

check_one() {
    # $1 = name; check that include/ and lib/ are populated.
    local name=$1
    local inc="$REPO_ROOT/externals/$name/include"
    local lib="$REPO_ROOT/externals/$name/lib"
    if [ -d "$inc" ] && [ -d "$lib" ] && [ -n "$(ls -A "$inc" 2>/dev/null)" ]; then
        log "$name: OK ($inc populated)"
        return 0
    else
        log "$name: MISSING (include/ or lib/ empty)"
        return 1
    fi
}

# ---------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------

ALL_DEPS="openssl zlib curl tcl unbound"
MODE=fetch

case "${1:-}" in
    --check)
        MODE=check; shift ;;
    --help|-h)
        sed -n '1,40p' "$0"; exit 0 ;;
esac

TARGETS="${@:-$ALL_DEPS}"

if [ "$MODE" = check ]; then
    RC=0
    for dep in $TARGETS; do
        check_one "$dep" || RC=1
    done
    exit $RC
fi

for dep in $TARGETS; do
    case "$dep" in
        openssl) bootstrap_openssl ;;
        zlib)    bootstrap_zlib ;;
        curl)    bootstrap_curl ;;
        tcl)     bootstrap_tcl ;;
        unbound) bootstrap_unbound ;;
        *) die "unknown dependency: $dep (one of: $ALL_DEPS)" ;;
    esac
done

log "done.  See each externals/<name>/README.md for the build step."
