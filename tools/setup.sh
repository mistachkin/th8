#!/bin/bash
###############################################################################
#
# setup.sh --
#
#     One-command setup of the compilation prerequisites for building
#     TH8 from a fresh Git checkout on Ubuntu (apt) or macOS (Homebrew).
#
#     A Git clone does NOT contain everything the build needs (unlike a
#     Fossil checkout of the full tree).  This script supplies the gaps:
#
#       * Git submodules -- externals/mimalloc/vendor and
#         externals/tommath/vendor (see .gitmodules).  Initialized here
#         with `git submodule update --init --recursive`.  The tommath
#         amalgamation the build consumes is generated from that
#         submodule by the Makefile's tommath_vendor target (needs
#         tclsh, installed below).
#
#       * System libraries the POSIX build links against: OpenSSL 3,
#         libcurl, zlib, Tcl 8.6, and libunbound.  Their
#         externals/{openssl,curl,zlib,tcl} trees are Win32-only and
#         git-ignored on POSIX, so on Ubuntu / macOS these come from the
#         system package manager.
#
#       * The toolchain: a C compiler (clang), make, pkg-config, git,
#         clang-format, and the LLVM tools llvm-cov / llvm-profdata used
#         by `make mcdc`.
#
#     NOT handled here (secret / opt-in / platform-specific):
#
#       * The test signing key tests/helpers/th8_test_key.snk
#         (semi-secret, git-ignored) only needed for (DEBUG)
#         builds that set ENABLE_TEST_KEY=1.
#       * The Cosmopolitan toolchain -- opt in via
#         tools/bootstrap_cosmo.sh.
#       * The Win32 / MSVC third-party libraries -- see
#         tools/bootstrap_win32_deps.sh.
#
# Usage:
#
#     bash tools/setup.sh                  # install prereqs + init submodules
#     bash tools/setup.sh --check          # report what is missing; install nothing
#     bash tools/setup.sh --no-submodules  # install packages only
#     bash tools/setup.sh --help           # show this usage
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

CHECK_ONLY=0
DO_SUBMODULES=1

# ---------------------------------------------------------------------
# Argument parsing.
# ---------------------------------------------------------------------

for arg in "$@"; do
    case "$arg" in
        --check)         CHECK_ONLY=1 ;;
        --no-submodules) DO_SUBMODULES=0 ;;
        -h|--help)
            sed -n '3,45p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "setup.sh: unknown option \"$arg\" (try --help)" >&2
            exit 2
            ;;
    esac
done

say()  { printf '==> %s\n' "$*"; }
warn() { printf 'setup.sh: warning: %s\n' "$*" >&2; }
die()  { printf 'setup.sh: error: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------
# Platform detection.
# ---------------------------------------------------------------------

OS=$(uname -s)

case "$OS" in
    Darwin) PLATFORM=macos ;;
    Linux)
        if command -v apt-get >/dev/null 2>&1; then
            PLATFORM=ubuntu
        else
            die "unsupported Linux distribution (this script targets Ubuntu/Debian apt).
       Install the equivalents of build-essential, clang, clang-format,
       llvm, pkg-config, libssl-dev, libcurl4-openssl-dev, zlib1g-dev,
       tcl-dev and libunbound-dev with your package manager, then run
       'git submodule update --init --recursive'."
        fi
        ;;
    *) die "unsupported OS \"$OS\" (supported: macOS, Ubuntu/Debian)." ;;
esac

say "Platform detected: $PLATFORM"

# ---------------------------------------------------------------------
# Presence checks (used by --check and to skip already-satisfied work).
#
# Each entry is "label|probe-command".  The probe returns 0 when the
# prerequisite is already available.
# ---------------------------------------------------------------------

have() { command -v "$1" >/dev/null 2>&1; }
have_pc() { pkg-config --exists "$1" 2>/dev/null; }

report_status() {
    local missing=0

    check_one() { # label  test-expr...
        local label="$1"; shift
        if "$@" >/dev/null 2>&1; then
            printf '  [ok]      %s\n' "$label"
        else
            printf '  [MISSING] %s\n' "$label"
            missing=1
        fi
    }

    say "Prerequisite status:"
    check_one "C compiler (cc/clang)"      sh -c 'command -v clang || command -v cc'
    check_one "make"                        have make
    check_one "pkg-config"                  have pkg-config
    check_one "git"                         have git
    check_one "clang-format"                have clang-format
    check_one "tclsh (Tcl 8.6+)"            sh -c 'command -v tclsh8.6 || command -v tclsh'
    if [ "$PLATFORM" = macos ]; then
        check_one "llvm-cov (xcrun)"        sh -c 'xcrun --find llvm-cov'
        check_one "llvm-profdata (xcrun)"   sh -c 'xcrun --find llvm-profdata'
    else
        check_one "llvm-cov"                sh -c 'command -v llvm-cov || command -v llvm-cov-* '
        check_one "llvm-profdata"           sh -c 'command -v llvm-profdata || command -v llvm-profdata-*'
    fi
    check_one "OpenSSL 3 (dev)"             sh -c 'pkg-config --exists openssl || pkg-config --exists libssl'
    check_one "libcurl (dev)"               sh -c 'command -v curl-config || pkg-config --exists libcurl'
    check_one "zlib (dev)"                  sh -c 'pkg-config --exists zlib || test -f /usr/include/zlib.h'
    check_one "libunbound (dev)"            sh -c 'pkg-config --exists libunbound || test -f /usr/include/unbound.h'

    return $missing
}

# ---------------------------------------------------------------------
# Package installation.
# ---------------------------------------------------------------------

install_macos() {
    have brew || die "Homebrew not found.  Install it from https://brew.sh then re-run."

    if ! xcode-select -p >/dev/null 2>&1; then
        warn "Xcode Command Line Tools not detected; installing (a GUI prompt may appear)."
        xcode-select --install || true
    fi

    # clang-format ships as its own formula (Xcode does not provide it);
    # llvm-cov / llvm-profdata come from Xcode via `xcrun`, so full llvm
    # is not required just to build or run `make mcdc`.
    local formulae=(
        openssl@3
        curl
        tcl-tk@8
        unbound
        pkg-config
        clang-format
        zlib
    )

    say "Installing Homebrew formulae: ${formulae[*]}"
    brew install "${formulae[@]}"

    say "Note: TH8's Makefile finds Tcl at \$(brew --prefix)/opt/tcl-tk@8 and"
    say "      OpenSSL at \$(brew --prefix openssl@3); no manual PATH edits needed."
}

install_ubuntu() {
    local SUDO=""
    if [ "$(id -u)" -ne 0 ]; then
        have sudo || die "need root or sudo to install apt packages."
        SUDO="sudo"
    fi

    local packages=(
        build-essential
        clang
        clang-format
        llvm
        make
        pkg-config
        git
        libssl-dev
        libcurl4-openssl-dev
        zlib1g-dev
        tcl
        tcl-dev
        libunbound-dev
        # libunbound's transitive libs, needed for fully-static links
        # (see the Makefile's UNBOUND_LIBS static fallback):
        nettle-dev
        libgmp-dev
        libevent-dev
    )

    say "Updating apt package lists"
    $SUDO apt-get update

    say "Installing apt packages: ${packages[*]}"
    $SUDO apt-get install -y "${packages[@]}"
}

# ---------------------------------------------------------------------
# Git submodules.
# ---------------------------------------------------------------------

init_submodules() {
    if [ ! -f "$REPO_ROOT/.gitmodules" ]; then
        warn "no .gitmodules found (not a Git checkout of the submodule tree?); skipping."
        return 0
    fi

    if ! git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        warn "$REPO_ROOT is not a Git working tree (Fossil checkout?); skipping submodules.
       On a Git clone, run: git submodule update --init --recursive"
        return 0
    fi

    say "Initializing Git submodules (externals/mimalloc/vendor, externals/tommath/vendor)"
    git -C "$REPO_ROOT" submodule update --init --recursive
}

# ---------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------

if [ "$CHECK_ONLY" -eq 1 ]; then
    if report_status; then
        say "All checked prerequisites are present."
        exit 0
    else
        say "Some prerequisites are missing; run 'bash tools/setup.sh' to install them."
        exit 1
    fi
fi

case "$PLATFORM" in
    macos)  install_macos ;;
    ubuntu) install_ubuntu ;;
esac

if [ "$DO_SUBMODULES" -eq 1 ]; then
    init_submodules
fi

echo
report_status || true
echo
say "Setup complete.  Build with:  make fresh   (or: make static shell)"
say "For ENABLE_TEST_KEY=1 builds you also need the (secret, git-ignored)"
say "test key tests/helpers/th8_test_key.snk, which this script does not install."
