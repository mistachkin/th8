#!/bin/bash
###############################################################################
#
# build-matrix.sh --
#
#     Test compile-time option combinations via "make fresh".
#
#     Two data files drive the matrix:
#
#       tools/data/compile-options.txt  -- ENABLE_* feature flags.
#           These have interdependencies, so they are tested in
#           combinations (single, pairwise, or exhaustive).
#
#       tools/data/plugin-options.txt   -- PLUGIN_* flags.
#           These are independently gateable, so each is toggled
#           off individually (no combinations needed).
#
#     The script runs four phases:
#       Phase 1: all-on baseline (all compile + all plugin options ON).
#       Phase 2: compile-option combinations (per --mode).
#       Phase 3: plugin toggles (each plugin OFF individually).
#       Phase 4: all-off composability build (EVERY compile option OFF at
#                once, plugins ON).  Feature use-sites are gated per-site on
#                exactly the feature(s) they need, so this all-off build must
#                link; it is the strongest check that no dependency is left
#                ungated.  See docs/public/portability.md section 7a.
#
# Usage:
#
#     bash tools/build-matrix.sh [options]
#
#     --mode MODE       Compile-option test mode: single, pairwise,
#                       or all (default: single)
#     --compile FILE    Compile options file
#                       (default: tools/data/compile-options.txt)
#     --plugins FILE    Plugin options file
#                       (default: tools/data/plugin-options.txt)
#     --no-plugins      Skip plugin toggles (phase 3)
#     --no-compile      Skip compile-option combinations (phase 2)
#     --no-alloff       Skip the all-off composability build (phase 4)
#     --target TARGET   Make target (default: fresh)
#     --extra "ARGS"    Extra arguments passed to make
#     --stop-on-error   Stop at the first failed build
#     --jobs N          Parallel make jobs (-jN)
#     --dry-run         Print commands without executing
#     --quiet           Suppress make output (show only pass/fail)
#     --log DIR         Write build logs to DIR (failures only
#                       by default; use --log-all for all builds)
#     --log-all         Keep logs for passing builds too
#     --help            Show this help
#
# Examples:
#
#     # Quick sanity: one baseline + each option off individually
#     bash tools/build-matrix.sh --mode single
#
#     # Pairwise compile options + individual plugin toggles
#     bash tools/build-matrix.sh --mode pairwise --quiet
#
#     # Exhaustive compile options (2^11 = 2048 builds) + plugins
#     bash tools/build-matrix.sh --mode all --quiet --log /tmp/matrix
#
#     # Only plugin toggles (skip compile combinations)
#     bash tools/build-matrix.sh --no-compile
#
#     # Only compile combinations (skip plugins)
#     bash tools/build-matrix.sh --mode pairwise --no-plugins
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set -euo pipefail

#
# Defaults.
#

MODE="single"
COMPILE_FILE="tools/data/compile-options.txt"
PLUGIN_FILE="tools/data/plugin-options.txt"
DO_COMPILE=1
DO_PLUGINS=1
DO_ALLOFF=1
TARGET="fresh-non-static"
EXTRA_ARGS=""
STOP_ON_ERROR=0
JOBS=""
DRY_RUN=0
QUIET=0
LOG_DIR=""
LOG_ALL=0

#
# Parse arguments.
#

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode)         MODE="$2"; shift 2 ;;
        --compile)      COMPILE_FILE="$2"; shift 2 ;;
        --plugins)      PLUGIN_FILE="$2"; shift 2 ;;
        --no-plugins)   DO_PLUGINS=0; shift ;;
        --no-compile)   DO_COMPILE=0; shift ;;
        --no-alloff)    DO_ALLOFF=0; shift ;;
        --target)       TARGET="$2"; shift 2 ;;
        --extra)        EXTRA_ARGS="$2"; shift 2 ;;
        --stop-on-error) STOP_ON_ERROR=1; shift ;;
        --jobs)         JOBS="-j$2"; shift 2 ;;
        --dry-run)      DRY_RUN=1; shift ;;
        --quiet)        QUIET=1; shift ;;
        --log)          LOG_DIR="$2"; shift 2 ;;
        --log-all)      LOG_ALL=1; shift ;;
        --help|-h)
            sed -n '3,/^###*$/{ /^#/{ s/^# \?//; p; }; }' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Use --help for usage." >&2
            exit 1
            ;;
    esac
done

#
# Read an options file into a named array variable.
#

read_options_into() {
    local file="$1"

    if [[ ! -f "$file" ]]; then
        echo "Error: options file not found: $file" >&2
        exit 1
    fi

    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ -z "$line" || "$line" = \#* ]] && continue
        echo "$line"
    done < "$file"
}

#
# Read both files.
#

COMPILE_OPTS=()
PLUGIN_OPTS=()

#
# Always load both option lists.  The --no-compile / --no-plugins flags gate
# whether their PHASES run, not whether the options are known: the baseline and
# the all-off composability build need the full compile-option list regardless.
#
while IFS= read -r opt; do
    COMPILE_OPTS+=("$opt")
done < <(read_options_into "$COMPILE_FILE")
while IFS= read -r opt; do
    PLUGIN_OPTS+=("$opt")
done < <(read_options_into "$PLUGIN_FILE")

NC=${#COMPILE_OPTS[@]}
NP=${#PLUGIN_OPTS[@]}

if [[ $NC -eq 0 && $NP -eq 0 ]]; then
    echo "Error: no options in $COMPILE_FILE or $PLUGIN_FILE." >&2
    exit 1
fi

#
# Validate mode.
#

case "$MODE" in
    single|pairwise|all) ;;
    *)
        echo "Error: unknown mode '$MODE' (use single, pairwise, or all)" >&2
        exit 1
        ;;
esac

#
# Estimate build count.
#
# Phase 1: 1 baseline
# Phase 2: compile-option combinations (depends on mode)
# Phase 3: NP plugin toggles (each individually)
#

COMPILE_BUILDS=0
if [[ $DO_COMPILE -eq 1 && $NC -gt 0 ]]; then
    case "$MODE" in
        single)   COMPILE_BUILDS=$NC ;;
        pairwise) COMPILE_BUILDS=$(( NC * (NC - 1) / 2 + NC )) ;;
        all)      COMPILE_BUILDS=$(( (1 << NC) - 1 )) ;;
    esac
fi

PLUGIN_BUILDS=0
if [[ $DO_PLUGINS -eq 1 && $NP -gt 0 ]]; then
    PLUGIN_BUILDS=$NP
fi

ALLOFF_BUILDS=0
if [[ $DO_ALLOFF -eq 1 && $NC -gt 0 ]]; then
    ALLOFF_BUILDS=1
fi

TOTAL=$(( 1 + COMPILE_BUILDS + PLUGIN_BUILDS + ALLOFF_BUILDS ))

echo "============================================"
echo "TH8 Build Matrix"
echo "============================================"
echo "Mode:             $MODE"
echo "Compile options:  $NC"
echo "Plugin options:   $NP"
echo "Builds:           $TOTAL"
echo "  Baseline:       1"
if [[ $COMPILE_BUILDS -gt 0 ]]; then
    echo "  Compile combos: $COMPILE_BUILDS"
fi
if [[ $PLUGIN_BUILDS -gt 0 ]]; then
    echo "  Plugin toggles: $PLUGIN_BUILDS"
fi
if [[ $ALLOFF_BUILDS -gt 0 ]]; then
    echo "  All-off build:  1"
fi
echo "Target:           $TARGET"
if [[ -n "$EXTRA_ARGS" ]]; then
    echo "Extra:            $EXTRA_ARGS"
fi
if [[ -n "$LOG_DIR" ]]; then
    echo "Logs:             $LOG_DIR"
    mkdir -p "$LOG_DIR"
fi
echo "============================================"
echo ""

#
# Build helpers.
#

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
BUILD_NUM=0
FAILED_BUILDS=""

run_build() {
    local label="$1"
    shift
    local make_args="$*"

    BUILD_NUM=$((BUILD_NUM + 1))

    local cmd="make $JOBS $TARGET $make_args $EXTRA_ARGS"

    if [[ $DRY_RUN -eq 1 ]]; then
        printf "[%d/%d] DRY-RUN: %s\n" "$BUILD_NUM" "$TOTAL" "$label"
        SKIP_COUNT=$((SKIP_COUNT + 1))
        return 0
    fi

    local rc=0
    local log_file=""

    if [[ -n "$LOG_DIR" ]]; then
        local safe_label="${label// /_}"
        safe_label="${safe_label//=/_}"
        safe_label="${safe_label//,/}"
        log_file="$LOG_DIR/$(printf '%04d' $BUILD_NUM)_${safe_label}.log"
    fi

    if [[ $QUIET -eq 1 ]]; then
        if [[ -n "$log_file" ]]; then
            $cmd > "$log_file" 2>&1 || rc=$?
        else
            $cmd > /dev/null 2>&1 || rc=$?
        fi
    else
        if [[ -n "$log_file" ]]; then
            $cmd 2>&1 | tee "$log_file" || rc=${PIPESTATUS[0]}
        else
            $cmd || rc=$?
        fi
    fi

    if [[ $rc -eq 0 ]]; then
        PASS_COUNT=$((PASS_COUNT + 1))
        printf "[%d/%d] PASS: %s\n" "$BUILD_NUM" "$TOTAL" "$label"
        if [[ -n "$log_file" && $LOG_ALL -eq 0 ]]; then
            rm -f "$log_file"
        fi
    else
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAILED_BUILDS="$FAILED_BUILDS  $label\n"
        printf "[%d/%d] FAIL: %s  (exit %d)\n" "$BUILD_NUM" "$TOTAL" "$label" "$rc"
        if [[ -n "$log_file" ]]; then
            echo "         Log: $log_file"
        fi
        if [[ $STOP_ON_ERROR -eq 1 ]]; then
            echo ""
            echo "Stopped on first error (--stop-on-error)."
            print_summary
            exit 1
        fi
    fi
}

#
# Build the "all-on" make arguments for both option sets.
#

all_on_args() {
    local args=""
    local i

    for ((i = 0; i < NC; i++)); do
        args="$args ${COMPILE_OPTS[$i]}=1"
    done
    for ((i = 0; i < NP; i++)); do
        args="$args ${PLUGIN_OPTS[$i]}=1"
    done
    echo "$args"
}

#
# all_off_args -- every compile (ENABLE_*) option OFF, plugins ON.  The
# composability build: a build with all optional features disabled at once.
# Plugins stay ON because an empty plugin set is a separate (degenerate) axis
# already covered by Phase 3, and a zero-length static plugin table is not a
# meaningful configuration.
#
all_off_args() {
    local args=""
    local i

    for ((i = 0; i < NC; i++)); do
        args="$args ${COMPILE_OPTS[$i]}=0"
    done
    for ((i = 0; i < NP; i++)); do
        args="$args ${PLUGIN_OPTS[$i]}=1"
    done
    echo "$args"
}

#
# Build make arguments for a compile-option bitmask.
# Plugins are always ON in compile-combo phases.
#

compile_args_for_mask() {
    local mask=$1
    local args=""
    local i

    for ((i = 0; i < NC; i++)); do
        if (( (mask >> i) & 1 )); then
            args="$args ${COMPILE_OPTS[$i]}=1"
        else
            args="$args ${COMPILE_OPTS[$i]}=0"
        fi
    done
    for ((i = 0; i < NP; i++)); do
        args="$args ${PLUGIN_OPTS[$i]}=1"
    done
    echo "$args"
}

#
# Build a label for a compile-option bitmask (shows OFF options).
#

compile_label_for_mask() {
    local mask=$1
    local off=""
    local i

    for ((i = 0; i < NC; i++)); do
        if ! (( (mask >> i) & 1 )); then
            if [[ -n "$off" ]]; then
                off="$off, "
            fi
            off="$off${COMPILE_OPTS[$i]}=0"
        fi
    done
    echo "$off"
}

#
# Build make arguments for a plugin toggle.
# All compile options ON, all plugins ON except the one being tested.
#

plugin_args_for_index() {
    local skip=$1
    local args=""
    local i

    for ((i = 0; i < NC; i++)); do
        args="$args ${COMPILE_OPTS[$i]}=1"
    done
    for ((i = 0; i < NP; i++)); do
        if [[ $i -eq $skip ]]; then
            args="$args ${PLUGIN_OPTS[$i]}=0"
        else
            args="$args ${PLUGIN_OPTS[$i]}=1"
        fi
    done
    echo "$args"
}

print_summary() {
    echo ""
    echo "============================================"
    echo "Build Matrix Summary"
    echo "============================================"
    echo "  Passed:  $PASS_COUNT"
    echo "  Failed:  $FAIL_COUNT"
    echo "  Skipped: $SKIP_COUNT"
    echo "  Total:   $BUILD_NUM / $TOTAL"
    if [[ $FAIL_COUNT -gt 0 ]]; then
        echo ""
        echo "Failed builds:"
        printf "$FAILED_BUILDS"
    fi
    echo "============================================"
}

###############################################################################
#
# Phase 1: Baseline (all options ON).
#
###############################################################################

echo "--- Phase 1: Baseline ---"
run_build "all-on" "$(all_on_args)"
echo ""

###############################################################################
#
# Phase 2: Compile-option combinations.
#
###############################################################################

if [[ $DO_COMPILE -eq 1 && $NC -gt 0 ]]; then
    echo "--- Phase 2: Compile options ($MODE, $NC options) ---"

    ALL_COMPILE_ON=$(( (1 << NC) - 1 ))

    case "$MODE" in
        single)
            for ((i = 0; i < NC; i++)); do
                mask=$(( ALL_COMPILE_ON ^ (1 << i) ))
                run_build "$(compile_label_for_mask $mask)" \
                    "$(compile_args_for_mask $mask)"
            done
            ;;

        pairwise)
            #
            # Single toggles.
            #
            for ((i = 0; i < NC; i++)); do
                mask=$(( ALL_COMPILE_ON ^ (1 << i) ))
                run_build "$(compile_label_for_mask $mask)" \
                    "$(compile_args_for_mask $mask)"
            done

            #
            # Pairwise toggles.
            #
            for ((i = 0; i < NC; i++)); do
                for ((j = i + 1; j < NC; j++)); do
                    mask=$(( ALL_COMPILE_ON ^ (1 << i) ^ (1 << j) ))
                    run_build "$(compile_label_for_mask $mask)" \
                        "$(compile_args_for_mask $mask)"
                done
            done
            ;;

        all)
            max=$(( 1 << NC ))

            if [[ $max -gt 10000 && $DRY_RUN -eq 0 ]]; then
                echo "WARNING: $max combinations." >&2
                echo "Press Ctrl-C to abort, or wait 5 seconds..." >&2
                sleep 5
            fi

            for ((mask = ALL_COMPILE_ON - 1; mask >= 0; mask--)); do
                run_build "$(compile_label_for_mask $mask)" \
                    "$(compile_args_for_mask $mask)"
            done
            ;;
    esac
    echo ""
fi

###############################################################################
#
# Phase 3: Plugin toggles (each individually).
#
###############################################################################

if [[ $DO_PLUGINS -eq 1 && $NP -gt 0 ]]; then
    echo "--- Phase 3: Plugin toggles ($NP plugins) ---"

    for ((i = 0; i < NP; i++)); do
        run_build "${PLUGIN_OPTS[$i]}=0" "$(plugin_args_for_index $i)"
    done
    echo ""
fi

###############################################################################
#
# Phase 4: All-off composability build (every compile option OFF at once).
#
###############################################################################

if [[ $ALLOFF_BUILDS -gt 0 ]]; then
    echo "--- Phase 4: All-off composability build (all $NC compile options OFF) ---"
    run_build "all-off" "$(all_off_args)"
    echo ""
fi

###############################################################################
#
# Summary.
#
###############################################################################

print_summary

if [[ $FAIL_COUNT -gt 0 ]]; then
    exit 1
fi

exit 0
