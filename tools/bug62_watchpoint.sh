#!/bin/sh
# Bug 62 hardware-watchpoint harness wrapper -- see tools/bug62_watchpoint.py
# and docs/internal/incomplete.md "Bug 62".
#
# Requires developer mode (one-time): sudo DevToolsSecurity -enable
# Requires a debug-info build of ./bin/th8sh (e.g. make ENABLE_TEST_KEY=1 debug).
#
# Usage: tools/bug62_watchpoint.sh [iterations]   (default 200)
set -e
ITERS="${1:-200}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"

if [ "$(DevToolsSecurity -status 2>/dev/null)" != "Developer mode is currently enabled." ]; then
    echo "WARNING: developer mode appears disabled; lldb may fail with"
    echo "         'cannot get permission to debug processes'."
    echo "         Enable once with: sudo DevToolsSecurity -enable"
fi

# ulimit -s: ASan/large debug frames trip TH8's native stack-limit guard at
# startup without a larger stack.
ulimit -s 65520 2>/dev/null || true

exec lldb -b \
    -o "command script import tools/bug62_watchpoint.py" \
    -o "wp62 ${ITERS}" \
    -- ./bin/th8sh
