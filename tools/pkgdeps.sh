#!/bin/sh
# tools/pkgdeps.sh -- install (or print) TH8's system dependencies for a named
# feature, reading the package manifest tools/data/packages.tsv.
#
# Usage:
#   pkgdeps.sh [--print] <feature> [<feature> ...]
#
#     feature : core | static | test  (see tools/data/packages.tsv)
#     --print : list the packages for this host's package manager and exit
#               WITHOUT installing anything.
#
# Host OS -> package manager: Darwin -> brew, Linux -> apt.  Override the
# detected manager with PKGDEPS_MANAGER=apt|brew (useful for testing).  Set
# SUDO= (empty) to run apt without sudo, e.g. as root inside a container;
# it defaults to "sudo".  brew is never run under sudo.
#
# Strictly POSIX sh; shellcheck-clean.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.

set -eu

prog=pkgdeps

# Resolve the manifest relative to this script so any CWD works.  Clear CDPATH
# first so a user's CDPATH cannot redirect the cd below.
CDPATH=''
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
data_file=$script_dir/data/packages.tsv

do_install=1
if [ "${1-}" = "--print" ]; then
    do_install=0
    shift
fi

if [ "$#" -eq 0 ]; then
    printf 'usage: %s [--print] <feature> [<feature> ...]\n' "$prog" >&2
    printf '       features: core static test\n' >&2
    exit 2
fi

if [ ! -r "$data_file" ]; then
    printf '%s: cannot read manifest: %s\n' "$prog" "$data_file" >&2
    exit 1
fi

# Determine the package manager (respecting an explicit override).
if [ -n "${PKGDEPS_MANAGER-}" ]; then
    manager=$PKGDEPS_MANAGER
else
    os=$(uname -s 2>/dev/null || echo Unknown)
    case $os in
        Darwin) manager=brew ;;
        Linux) manager=apt ;;
        *)
            printf '%s: unsupported OS "%s" -- install the packages in %s manually.\n' \
                "$prog" "$os" "$data_file" >&2
            exit 1
            ;;
    esac
fi

case $manager in
    apt | brew) ;;
    *)
        printf '%s: unknown manager "%s" (expected apt or brew).\n' \
            "$prog" "$manager" >&2
        exit 1
        ;;
esac

# manifest_field FEATURE KEY -> the TAB-third field of the matching row, or ""
# (KEY is a manager name such as "apt", "brew", "note-brew").  Comment rows
# (first field beginning with "#") are ignored.
manifest_field() {
    awk -F '\t' -v f="$1" -v k="$2" '
        index($1, "#") == 1 { next }
        $1 == f && $2 == k { print $3; exit }
    ' "$data_file"
}

apt_updated=0

install_apt() {
    if ! command -v apt-get >/dev/null 2>&1; then
        printf '%s: apt-get not found (this manager is for Debian/Ubuntu).\n' \
            "$prog" >&2
        exit 1
    fi
    if [ "$apt_updated" -eq 0 ]; then
        ${SUDO-sudo} apt-get update
        apt_updated=1
    fi
    # Word-splitting of $1 is intentional: pass each package as its own arg.
    # shellcheck disable=SC2086
    ${SUDO-sudo} apt-get install -y $1
}

install_brew() {
    if ! command -v brew >/dev/null 2>&1; then
        printf '%s: brew not found -- install Homebrew first (https://brew.sh).\n' \
            "$prog" >&2
        exit 1
    fi
    # Word-splitting of $1 is intentional (see above).  brew runs unprivileged.
    # shellcheck disable=SC2086
    brew install $1
}

# macOS has NO clang-format-19 Homebrew formula (only the unversioned latest),
# so set the pinned version up in an isolated pip venv and expose it as
# `clang-format-19` -- the exact name `make audit-format` looks for.  Linux gets
# clang-format-19 from apt, so this is brew/core only.  Idempotent and
# best-effort: a problem warns but does NOT fail the dependency install.
ensure_clang_format_brew() {
    cf_bin=$(command -v clang-format-19 2>/dev/null || true)
    if [ -n "$cf_bin" ]; then
        cf_ver=$("$cf_bin" --version 2>/dev/null |
            sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' || true)
        if [ "${cf_ver:-}" = "19" ]; then
            printf '%s: clang-format-19 already present (%s)\n' "$prog" "$cf_bin" >&2
            return 0
        fi
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        printf '%s: WARNING: python3 not found -- set up clang-format-19 manually (docs/public/portability.md).\n' \
            "$prog" >&2
        return 0
    fi

    cf_venv="$HOME/.clang-format-19-venv"
    cf_link="$(brew --prefix 2>/dev/null || echo /opt/homebrew)/bin/clang-format-19"

    if [ ! -x "$cf_venv/bin/clang-format" ]; then
        if ! python3 -m venv "$cf_venv"; then
            printf '%s: WARNING: could not create venv %s.\n' "$prog" "$cf_venv" >&2
            return 0
        fi
        if ! "$cf_venv/bin/pip" install --quiet 'clang-format==19.1.7'; then
            printf '%s: WARNING: pip install clang-format==19.1.7 failed.\n' \
                "$prog" >&2
            return 0
        fi
    fi
    ln -sf "$cf_venv/bin/clang-format" "$cf_link"
    printf '%s: clang-format-19 ready at %s (pinned pip venv)\n' \
        "$prog" "$cf_link" >&2
}

# Debian/Ubuntu ship the interpreter as `tclsh8.6`, but the Makefile and build
# tooling call bare `tclsh`.  Link it if absent so `make` works right after
# `pkg-deps-core` (apt/core only; the macOS keg-only tcl-tk@8 goes on PATH by
# the caller).  Idempotent and best-effort.
ensure_tclsh_apt() {
    if command -v tclsh >/dev/null 2>&1; then
        return 0
    fi
    tclsh_bin=$(command -v tclsh8.6 2>/dev/null || true)
    if [ -z "$tclsh_bin" ]; then
        printf '%s: WARNING: neither tclsh nor tclsh8.6 found after install.\n' \
            "$prog" >&2
        return 0
    fi
    if ${SUDO-sudo} ln -sf "$tclsh_bin" /usr/local/bin/tclsh; then
        printf '%s: linked tclsh -> %s\n' "$prog" "$tclsh_bin" >&2
    fi
}

status=0

for feature in "$@"; do
    pkgs=$(manifest_field "$feature" "$manager")
    note=$(manifest_field "$feature" "note-$manager")

    if [ -n "$note" ]; then
        printf '%s: note (%s/%s): %s\n' "$prog" "$manager" "$feature" "$note" >&2
    fi

    if [ -z "$pkgs" ]; then
        # Distinguish "feature exists but nothing for THIS manager" (fine) from
        # a typo'd/unknown feature (error).
        if [ -n "$(manifest_field "$feature" apt)$(manifest_field "$feature" brew)" ]; then
            printf '%s: no %s packages for feature "%s".\n' \
                "$prog" "$manager" "$feature" >&2
        else
            printf '%s: unknown feature "%s" (see %s).\n' \
                "$prog" "$feature" "$data_file" >&2
            status=1
        fi
        continue
    fi

    if [ "$do_install" -eq 0 ]; then
        printf '%s\n' "$pkgs"
        continue
    fi

    printf '%s: installing %s packages for "%s": %s\n' \
        "$prog" "$manager" "$feature" "$pkgs" >&2
    case $manager in
        apt) install_apt "$pkgs" ;;
        brew) install_brew "$pkgs" ;;
    esac

    # Post-install env glue for the `core` feature so `make` works immediately:
    #   macOS -- clang-format-19 has no brew formula; provision the pinned venv.
    #   Linux -- link bare `tclsh` -> `tclsh8.6`.
    if [ "$feature" = "core" ]; then
        case $manager in
            brew) ensure_clang_format_brew ;;
            apt) ensure_tclsh_apt ;;
        esac
    fi
done

exit "$status"
