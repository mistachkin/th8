#!/usr/bin/env bash
#
# fuzz_rotate.sh -- 24-hour rotating AFL++ fuzz campaign driver for TH8.
#
# Drives the six TH8 fuzz targets (eval, expr, list, format, harpy,
# snk) over a 24-hour wall-clock window, running as many in parallel
# as the host has CPU cores.
#
# Default schedule (TOTAL_HOURS=24, PHASE_HOURS=8, PARALLEL=4):
#
#   Phase 1 (hours  0- 8):  eval, expr, list, format
#   Phase 2 (hours  8-16):  harpy, snk, eval, expr
#   Phase 3 (hours 16-24):  list, format, harpy, snk
#
# Each fuzzer is scheduled into exactly two phases for 16 wall-clock
# hours of fuzzing.  AFL coverage state persists between phase visits
# (using "-i -" resume) so subsequent slots continue evolving the
# corpus rather than starting over.
#
# Subcommands:
#   start    Build (if needed) and run the rotation in the foreground.
#   plan     Print the schedule and exit.
#   build    Build the AFL fuzz binaries (FUZZ_MODE=afl) and exit.
#   status   Show afl-whatsup for each target.
#   stop     Send SIGINT to a running rotation and all afl-fuzz kids.
#   summary  Print exec counts, crashes, and hangs per fuzzer.
#   help     Show this help.
#
# Configuration (env vars, override at invocation):
#   TOTAL_HOURS   Total wall-clock budget (default: 24).
#   PHASE_HOURS   Hours per phase; must divide TOTAL_HOURS (default: 8).
#   PARALLEL      Concurrent fuzzers per phase (default: 4).
#                 Capped to NUM_FUZZERS (6).
#   FUZZ_OUT      Output root (default: build/fuzz_rotate under repo).
#   FUZZ_BIN      AFL binary dir (default: bin-afl under repo).
#
# Long-running use:
#   nohup tools/fuzz_rotate.sh start > rotate.log 2>&1 &
#     -- or run inside tmux/screen.  Check progress from another
#     shell with 'tools/fuzz_rotate.sh status'.
#

set -Eeuo pipefail

# ---------------------------------------------------------------- paths
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

ALL_FUZZERS=(eval expr list format harpy snk)
NUM_FUZZERS=${#ALL_FUZZERS[@]}

TOTAL_HOURS="${TOTAL_HOURS:-24}"
PHASE_HOURS="${PHASE_HOURS:-8}"
PARALLEL="${PARALLEL:-4}"

FUZZ_OUT="${FUZZ_OUT:-$PROJECT_ROOT/build/fuzz_rotate}"
FUZZ_BIN="${FUZZ_BIN:-$PROJECT_ROOT/bin-afl}"

STATE_DIR="$FUZZ_OUT/state"
LOG_DIR="$FUZZ_OUT/logs"
RUN_DIR="$FUZZ_OUT/run"
CRASHES_DIR="$FUZZ_OUT/crashes"
HANGS_DIR="$FUZZ_OUT/hangs"
PID_FILE="$STATE_DIR/orchestrator.pid"
LOCK_FILE="$STATE_DIR/orchestrator.lock"
SCHEDULE_FILE="$STATE_DIR/schedule.txt"
SUMMARY_FILE="$FUZZ_OUT/summary.txt"

# Cap PARALLEL at NUM_FUZZERS so we never duplicate a fuzzer within
# the same phase.
if (( PARALLEL > NUM_FUZZERS )); then
    PARALLEL=$NUM_FUZZERS
fi
if (( PARALLEL < 1 )); then
    echo "PARALLEL must be >= 1" >&2
    exit 2
fi

# ----------------------------------------------------------- logging
log() {
    printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

die() {
    log "ERROR: $*" >&2
    exit 1
}

# ----------------------------------------------------------- schedule
#
# Round-robin assignment: lay out NUM_PHASES * PARALLEL slots, fill
# them in order from a queue of the fuzzer names repeated as many
# times as needed.  For the canonical 6-fuzzer / 4-parallel / 3-phase
# case this gives a perfectly balanced 16h-per-fuzzer schedule with
# no duplicates within any single phase.
#
generate_schedule() {
    local total_phases=$(( TOTAL_HOURS / PHASE_HOURS ))
    if (( total_phases * PHASE_HOURS != TOTAL_HOURS )); then
        die "TOTAL_HOURS ($TOTAL_HOURS) is not divisible by PHASE_HOURS ($PHASE_HOURS)"
    fi
    if (( total_phases < 1 )); then
        die "computed 0 phases (TOTAL_HOURS=$TOTAL_HOURS, PHASE_HOURS=$PHASE_HOURS)"
    fi
    local total_slots=$(( total_phases * PARALLEL ))
    local reps=$(( (total_slots + NUM_FUZZERS - 1) / NUM_FUZZERS ))
    local -a queue=()
    local i j
    for (( i = 0; i < reps; i++ )); do
        for (( j = 0; j < NUM_FUZZERS; j++ )); do
            queue+=("${ALL_FUZZERS[$j]}")
        done
    done
    for (( i = 0; i < total_phases; i++ )); do
        local start=$(( i * PARALLEL ))
        local line="$i"
        for (( j = 0; j < PARALLEL; j++ )); do
            line+=" ${queue[$((start + j))]}"
        done
        echo "$line"
    done
}

# ----------------------------------------------------------- AFL run
run_phase() {
    local phase_idx="$1"; shift
    local -a fuzzers=("$@")
    # PHASE_SECONDS, if set, overrides PHASE_HOURS * 3600.  Intended
    # for short integration tests.
    local phase_secs=$(( PHASE_HOURS * 3600 ))
    if [[ -n "${PHASE_SECONDS:-}" ]]; then
        phase_secs="$PHASE_SECONDS"
    fi
    log "=== Phase $phase_idx start (${PHASE_HOURS}h, ${#fuzzers[@]} fuzzers): ${fuzzers[*]} ==="
    local -a pids=()
    local f
    for f in "${fuzzers[@]}"; do
        local bin="$FUZZ_BIN/fuzz_$f"
        if [[ ! -x "$bin" ]]; then
            die "missing fuzz binary: $bin (run 'tools/fuzz_rotate.sh build')"
        fi
        local corpus_dir="$PROJECT_ROOT/fuzz/corpus_$f"
        local out_dir="$RUN_DIR/$f"
        local log_file="$LOG_DIR/phase_${phase_idx}_${f}.log"
        mkdir -p "$out_dir"
        # Resume from existing queue if this fuzzer has run before.
        local -a input_arg=("-i" "$corpus_dir")
        if [[ -d "$out_dir/default/queue" ]] \
            && [[ -n "$(ls -A "$out_dir/default/queue" 2>/dev/null || true)" ]]; then
            input_arg=("-i" "-")
            log "  $f: resuming from $out_dir"
        else
            log "  $f: fresh from $corpus_dir"
        fi
        # Launch the fuzzer.  -V auto-exits after the phase budget.
        # -m none lets ASan use its own RSS; AFL_SKIP_CPUFREQ and
        # AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES suppress preflight
        # bail-outs on this host (no scaling_governor, apport is the
        # core handler).
        AFL_NO_UI=1 \
        AFL_SKIP_CPUFREQ=1 \
        AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
        AFL_NO_AFFINITY=1 \
        afl-fuzz \
            "${input_arg[@]}" \
            -o "$out_dir" \
            -V "$phase_secs" \
            -m none \
            -- "$bin" \
            > "$log_file" 2>&1 &
        local pid=$!
        pids+=("$pid")
        log "  $f: afl-fuzz pid=$pid (log $log_file)"
    done
    printf '%s\n' "${pids[@]}" > "$STATE_DIR/phase_${phase_idx}.pids"
    # Wait for every child; if any was killed by signal, propagate so
    # the orchestrator can stop the rest.
    local pid rc
    for pid in "${pids[@]}"; do
        if wait "$pid"; then
            rc=0
        else
            rc=$?
        fi
        log "  pid $pid exited rc=$rc"
    done
    rm -f "$STATE_DIR/phase_${phase_idx}.pids"
    log "=== Phase $phase_idx complete ==="
}

# ----------------------------------------------------- crash gather
collect_findings() {
    local phase_idx="$1"
    local f out
    for f in "${ALL_FUZZERS[@]}"; do
        out="$RUN_DIR/$f/default"
        [[ -d "$out" ]] || continue
        if [[ -d "$out/crashes" ]]; then
            local cdst="$CRASHES_DIR/$f"
            mkdir -p "$cdst"
            # cp -n: never overwrite an existing copy.  Crashes are
            # named id:NNNNNN,sig:* so two runs with the same finding
            # id don't collide unless they really are the same input.
            local crashed
            crashed=$(find "$out/crashes" -type f -name 'id:*' 2>/dev/null | wc -l)
            if (( crashed > 0 )); then
                find "$out/crashes" -type f -name 'id:*' \
                    -exec cp -n {} "$cdst/" \;
                log "  crashes($f): $crashed total in $out/crashes"
            fi
        fi
        if [[ -d "$out/hangs" ]]; then
            local hdst="$HANGS_DIR/$f"
            mkdir -p "$hdst"
            local hanged
            hanged=$(find "$out/hangs" -type f -name 'id:*' 2>/dev/null | wc -l)
            if (( hanged > 0 )); then
                find "$out/hangs" -type f -name 'id:*' \
                    -exec cp -n {} "$hdst/" \;
                log "  hangs($f): $hanged total in $out/hangs"
            fi
        fi
    done
}

# ------------------------------------------------------------- stop
stop_all() {
    log "stopping afl-fuzz children..."
    # Direct descendants first, then any orphans by name match.
    pkill -INT -P "$$" afl-fuzz 2>/dev/null || true
    sleep 1
    pkill -INT -f 'afl-fuzz.*fuzz_(eval|expr|list|format|harpy|snk)' 2>/dev/null || true
    sleep 2
    if pgrep -f 'afl-fuzz.*fuzz_' >/dev/null 2>&1; then
        log "some afl-fuzz processes still alive; sending SIGTERM..."
        pkill -TERM -f 'afl-fuzz.*fuzz_(eval|expr|list|format|harpy|snk)' 2>/dev/null || true
        sleep 2
    fi
    if pgrep -f 'afl-fuzz.*fuzz_' >/dev/null 2>&1; then
        log "still alive; sending SIGKILL..."
        pkill -KILL -f 'afl-fuzz.*fuzz_(eval|expr|list|format|harpy|snk)' 2>/dev/null || true
    fi
}

# ----------------------------------------------------- subcommands
cmd_plan() {
    mkdir -p "$STATE_DIR"
    local schedule
    schedule="$(generate_schedule)"
    local total_phases total_slots
    total_phases=$(( TOTAL_HOURS / PHASE_HOURS ))
    total_slots=$(( total_phases * PARALLEL ))
    echo "Fuzz Rotation Plan"
    echo "  Total hours : $TOTAL_HOURS"
    echo "  Phase hours : $PHASE_HOURS"
    echo "  Parallel    : $PARALLEL  (of $NUM_FUZZERS targets)"
    echo "  Total phases: $total_phases"
    echo "  Total slots : $total_slots"
    echo "  Output      : $FUZZ_OUT"
    if (( total_slots % NUM_FUZZERS != 0 )); then
        echo
        echo "  NOTE: total slots ($total_slots) is not a multiple of"
        echo "        NUM_FUZZERS ($NUM_FUZZERS); some fuzzers will get"
        echo "        one fewer slot than others.  For balanced time"
        echo "        choose PHASE_HOURS and PARALLEL so total_slots is"
        echo "        a multiple of $NUM_FUZZERS."
    fi
    echo
    echo "Schedule:"
    local line idx rest start end
    while IFS= read -r line; do
        idx="${line%% *}"
        rest="${line#* }"
        start=$(( idx * PHASE_HOURS ))
        end=$(( (idx + 1) * PHASE_HOURS ))
        printf "  Phase %d (hours %2d-%2d): %s\n" "$idx" "$start" "$end" "$rest"
    done <<< "$schedule"
    echo
    declare -A counts
    while IFS= read -r line; do
        rest="${line#* }"
        for f in $rest; do
            counts[$f]=$(( ${counts[$f]:-0} + PHASE_HOURS ))
        done
    done <<< "$schedule"
    echo "Per-fuzzer wall-clock hours:"
    for f in "${ALL_FUZZERS[@]}"; do
        printf "  %-7s %d\n" "$f" "${counts[$f]:-0}"
    done
}

cmd_build() {
    mkdir -p "$LOG_DIR"
    log "building fuzz binaries (FUZZ_MODE=afl, output $FUZZ_BIN) ..."
    make FUZZ_MODE=afl fuzz 2>&1 | tee "$LOG_DIR/build.log"
    local missing=()
    local f
    for f in "${ALL_FUZZERS[@]}"; do
        if [[ ! -x "$FUZZ_BIN/fuzz_$f" ]]; then
            missing+=("$f")
        fi
    done
    if (( ${#missing[@]} > 0 )); then
        die "missing fuzz binaries after build: ${missing[*]}"
    fi
    log "all fuzz binaries built."
}

cmd_start() {
    mkdir -p "$STATE_DIR" "$LOG_DIR" "$RUN_DIR" "$CRASHES_DIR" "$HANGS_DIR"
    # Single-instance lock: refuse to start a second orchestrator
    # against the same FUZZ_OUT.
    exec 9>"$LOCK_FILE"
    if ! flock -n 9; then
        die "another rotation is in progress (lock $LOCK_FILE)"
    fi
    echo "$$" > "$PID_FILE"
    # Build binaries if any are missing.
    local missing=() f
    for f in "${ALL_FUZZERS[@]}"; do
        if [[ ! -x "$FUZZ_BIN/fuzz_$f" ]]; then
            missing+=("$f")
        fi
    done
    if (( ${#missing[@]} > 0 )); then
        log "missing binaries (${missing[*]}); building first..."
        cmd_build
    fi
    # SIGINT/SIGTERM → stop everything cleanly.
    trap 'log "signal received, stopping ..."; stop_all; rm -f "$PID_FILE"; exit 130' INT TERM
    log "=========================================================="
    log "Fuzz rotation starting"
    log "  Project    : $PROJECT_ROOT"
    log "  Output     : $FUZZ_OUT"
    log "  Wall-clock : ${TOTAL_HOURS}h  Phase: ${PHASE_HOURS}h  Parallel: $PARALLEL"
    log "  Fuzzers    : ${ALL_FUZZERS[*]}"
    log "=========================================================="
    generate_schedule > "$SCHEDULE_FILE"
    log "schedule saved to $SCHEDULE_FILE"
    local line phase_idx
    while IFS= read -r line; do
        phase_idx="${line%% *}"
        # shellcheck disable=SC2206  # intentional word-split
        local -a fuzzers=(${line#* })
        run_phase "$phase_idx" "${fuzzers[@]}"
        collect_findings "$phase_idx"
    done < "$SCHEDULE_FILE"
    log "=========================================================="
    log "Fuzz rotation finished"
    log "=========================================================="
    cmd_summary | tee "$SUMMARY_FILE"
    rm -f "$PID_FILE"
}

cmd_stop() {
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid="$(cat "$PID_FILE")"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            log "sending SIGINT to orchestrator pid=$pid"
            kill -INT "$pid" || true
            # Give the orchestrator's trap time to act.
            local i
            for (( i = 0; i < 10; i++ )); do
                if ! kill -0 "$pid" 2>/dev/null; then
                    break
                fi
                sleep 1
            done
        fi
    fi
    # Sweep up anything left over.
    stop_all
    rm -f "$PID_FILE"
}

cmd_status() {
    echo "Output: $FUZZ_OUT"
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid="$(cat "$PID_FILE")"
        if kill -0 "$pid" 2>/dev/null; then
            echo "Orchestrator: running (pid=$pid)"
        else
            echo "Orchestrator: PID file present but process gone"
        fi
    else
        echo "Orchestrator: not running"
    fi
    if [[ -f "$SCHEDULE_FILE" ]]; then
        echo
        echo "Schedule:"
        sed 's/^/  /' "$SCHEDULE_FILE"
    fi
    echo
    echo "Live afl-fuzz processes:"
    if pgrep -af 'afl-fuzz.*fuzz_' >/dev/null 2>&1; then
        pgrep -af 'afl-fuzz.*fuzz_' | sed 's/^/  /'
    else
        echo "  (none)"
    fi
    echo
    if command -v afl-whatsup >/dev/null 2>&1; then
        local f out
        for f in "${ALL_FUZZERS[@]}"; do
            out="$RUN_DIR/$f"
            if [[ -d "$out" ]]; then
                echo "=== $f ==="
                afl-whatsup -s "$out" 2>/dev/null || true
                echo
            fi
        done
    fi
}

cmd_summary() {
    echo "Fuzz Rotation Summary  ($(date '+%Y-%m-%d %H:%M:%S'))"
    echo "Output: $FUZZ_OUT"
    echo
    printf "  %-7s %14s %10s %10s\n" "fuzzer" "execs_done" "crashes" "hangs"
    local f out execs crashes hangs
    for f in "${ALL_FUZZERS[@]}"; do
        out="$RUN_DIR/$f/default"
        execs="-"; crashes=0; hangs=0
        if [[ -f "$out/fuzzer_stats" ]]; then
            execs="$(awk -F': *' '/^execs_done/ {print $2; exit}' "$out/fuzzer_stats")"
        fi
        if [[ -d "$out/crashes" ]]; then
            crashes=$(find "$out/crashes" -type f -name 'id:*' 2>/dev/null | wc -l)
        fi
        if [[ -d "$out/hangs" ]]; then
            hangs=$(find "$out/hangs" -type f -name 'id:*' 2>/dev/null | wc -l)
        fi
        printf "  %-7s %14s %10d %10d\n" "$f" "$execs" "$crashes" "$hangs"
    done
    echo
    echo "Aggregated crashes: $CRASHES_DIR"
    echo "Aggregated hangs:   $HANGS_DIR"
}

cmd_help() {
    sed -n '2,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

main() {
    local cmd="${1:-help}"
    shift || true
    case "$cmd" in
        start)   cmd_start "$@" ;;
        stop)    cmd_stop "$@" ;;
        status)  cmd_status "$@" ;;
        summary) cmd_summary "$@" ;;
        plan)    cmd_plan "$@" ;;
        build)   cmd_build "$@" ;;
        help|-h|--help) cmd_help ;;
        *)
            echo "unknown command: $cmd" >&2
            cmd_help >&2
            exit 2
            ;;
    esac
}

main "$@"
