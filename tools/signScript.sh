#!/bin/bash
###############################################################################
#
# signScript.sh --
#
#     Sign a TH8 script file using the test signing key.
#
#     Creates a companion .b64sig signature file alongside the script.
#     The script file is read, piped through signScript.th8 (which does
#     the actual RSA signing), and the signature is written to
#     <scriptFile>.b64sig.
#
# Usage:
#
#     bash tools/signScript.sh <scriptFile> [th8sh]
#
#     Arguments:
#       scriptFile  -- Path to the .tcl script to sign.
#       th8sh       -- Optional path to the TH8 shell (default: bin/th8sh).
#
# Examples:
#
#     bash tools/signScript.sh tests/hello.tcl
#     bash tools/signScript.sh tests/hello.tcl ./bin/th8sh
#
# Notes:
#     - Must be run from the project root directory.
#     - Requires a debug build (TH8_DEBUG) or TH8SH_YES_TESTLIB for
#       the test signing key to be available.
#     - The signed-only policy must be active for load_snk to work.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set -e

SCRIPT_FILE="$1"
TH8SH="${2:-bin/th8sh}"
SIGNING_TOOL="tools/signScript.th8"
SIGNATURE_FILE="${SCRIPT_FILE}.b64sig"

if [ -z "$SCRIPT_FILE" ]; then
    echo "Usage: $0 <scriptFile> [th8sh]" >&2
    exit 1
fi

if [ ! -f "$SCRIPT_FILE" ]; then
    echo "Error: script file not found: $SCRIPT_FILE" >&2
    exit 1
fi

if [ ! -x "$TH8SH" ]; then
    echo "Error: th8sh not found or not executable: $TH8SH" >&2
    exit 1
fi

if [ ! -f "$SIGNING_TOOL" ]; then
    echo "Error: signing tool not found: $SIGNING_TOOL" >&2
    exit 1
fi

#
# Run the TH8 signing tool.
# - The signed-only policy must be active for [harpy sign] to work,
#   so we do NOT set TH8SH_NO_SCRIPT_SECURITY.
# - The signing tool itself is sourced via -e to bypass the
#   signed-only check (SaveSignedOnly is active for -e).
#

"$TH8SH" -e "source $SIGNING_TOOL" < "$SCRIPT_FILE" > "$SIGNATURE_FILE"

if [ $? -eq 0 ] && [ -s "$SIGNATURE_FILE" ]; then
    echo "Signed: $SCRIPT_FILE -> $SIGNATURE_FILE"
else
    echo "Error: signing failed for $SCRIPT_FILE" >&2
    rm -f "$SIGNATURE_FILE"
    exit 1
fi
