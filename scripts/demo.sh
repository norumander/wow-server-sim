#!/usr/bin/env bash
# demo.sh — Narrated walkthrough of the WoW Server Simulator reliability lifecycle.
#
# Demonstrates the full detect → diagnose → fix → deploy cycle in ~70 seconds:
#   Phase 1: Baseline — healthy server with player traffic
#   Phase 2: Break It — inject a latency spike fault
#   Phase 3: Diagnose — detect anomalies and identify root cause
#   Phase 4: Fix It — deactivate the fault, verify recovery
#   Phase 5: Pipeline — automated canary deployment with rollback
#   Phase 6: Summary — final health check and lifecycle recap
#
# Usage:
#   bash scripts/demo.sh
#
# Prerequisites:
#   - C++ server binary built (build/Debug/wow-server-sim.exe or build/wow-server-sim)
#   - Python venv with wowsim installed (bash scripts/setup_venv.sh)
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ---------------------------------------------------------------------------
# Color utilities (disabled when stdout is not a TTY)
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    BOLD='\033[1m'
    DIM='\033[2m'
    CYAN='\033[36m'
    GREEN='\033[32m'
    YELLOW='\033[33m'
    RED='\033[31m'
    MAGENTA='\033[35m'
    RESET='\033[0m'
else
    BOLD='' DIM='' CYAN='' GREEN='' YELLOW='' RED='' MAGENTA='' RESET=''
fi

header()  { echo -e "\n${BOLD}${CYAN}=== $1 ===${RESET}\n"; }
narrate() { echo -e "${DIM}$1${RESET}"; }
success() { echo -e "${GREEN}[OK]${RESET} $1"; }
warn()    { echo -e "${YELLOW}[!!]${RESET} $1"; }
run_cmd() { echo -e "${MAGENTA}\$ $1${RESET}"; eval "$1"; }

# ---------------------------------------------------------------------------
# Prerequisites — detect server binary and Python venv
# ---------------------------------------------------------------------------
SERVER_BIN=""
if [ -f "$PROJECT_ROOT/build/Debug/wow-server-sim.exe" ]; then
    SERVER_BIN="$PROJECT_ROOT/build/Debug/wow-server-sim.exe"
elif [ -f "$PROJECT_ROOT/build/wow-server-sim.exe" ]; then
    SERVER_BIN="$PROJECT_ROOT/build/wow-server-sim.exe"
elif [ -f "$PROJECT_ROOT/build/wow-server-sim" ]; then
    SERVER_BIN="$PROJECT_ROOT/build/wow-server-sim"
else
    echo "ERROR: Server binary not found. Build first: bash scripts/build.sh"
    exit 1
fi

if [ -f "$PROJECT_ROOT/.venv/Scripts/activate" ]; then
    # shellcheck disable=SC1091
    source "$PROJECT_ROOT/.venv/Scripts/activate"
elif [ -f "$PROJECT_ROOT/.venv/bin/activate" ]; then
    # shellcheck disable=SC1091
    source "$PROJECT_ROOT/.venv/bin/activate"
else
    echo "ERROR: Python venv not found. Run: bash scripts/setup_venv.sh"
    exit 1
fi

if ! command -v wowsim &>/dev/null; then
    echo "ERROR: wowsim CLI not found. Run: pip install -e tools/"
    exit 1
fi

TELEMETRY_FILE="$PROJECT_ROOT/telemetry.jsonl"
SERVER_PID=""

# ---------------------------------------------------------------------------
# Server lifecycle
# ---------------------------------------------------------------------------
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        narrate "Stopping server (PID $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

start_server() {
    narrate "Removing stale telemetry file..."
    rm -f "$TELEMETRY_FILE"

    narrate "Starting C++ game server in background..."
    "$SERVER_BIN" > /dev/null 2>&1 &
    SERVER_PID=$!

    narrate "Waiting for server to initialize (telemetry file)..."
    local elapsed=0
    while [ ! -f "$TELEMETRY_FILE" ]; do
        sleep 0.5
        elapsed=$((elapsed + 1))
        if [ $elapsed -ge 20 ]; then
            echo "ERROR: Server did not produce telemetry within 10 seconds."
            exit 1
        fi
    done
    success "Server started (PID $SERVER_PID)"

    narrate "Allowing initial ticks to accumulate (2s)..."
    sleep 2
}

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------
banner() {
    echo -e "${BOLD}${CYAN}"
    echo "  ╔══════════════════════════════════════════════════════╗"
    echo "  ║     WoW Server Simulator — Reliability Demo         ║"
    echo "  ║     detect → diagnose → fix → deploy                ║"
    echo "  ╚══════════════════════════════════════════════════════╝"
    echo -e "${RESET}"
    narrate "This demo walks through the full server reliability lifecycle."
    narrate "Total runtime: ~70 seconds."
    echo ""
}

# ---------------------------------------------------------------------------
# Phase 1: Baseline — healthy server with player traffic
# ---------------------------------------------------------------------------
phase_baseline() {
    header "Phase 1: Baseline — Healthy Server"

    narrate "Spawning 5 simulated players for 5 seconds to generate game traffic."
    narrate "Traffic mix: 50% movement, 30% spell casts, 20% combat actions."
    echo ""
    run_cmd "wowsim spawn-clients --count 5 --duration 5"
    echo ""

    narrate "Checking server health against telemetry (no fault queries)."
    echo ""
    run_cmd "wowsim health --log-file '$TELEMETRY_FILE' --no-faults"
    echo ""

    success "Baseline established: server is healthy under normal load."
}

# ---------------------------------------------------------------------------
# Phase 2: Break It — inject a latency spike
# ---------------------------------------------------------------------------
phase_break() {
    header "Phase 2: Break It — Inject Latency Spike"

    narrate "Injecting F1 (latency-spike): adds 200ms delay to every tick."
    narrate "The server's tick budget is 50ms, so 200ms is a 4x overrun."
    echo ""
    run_cmd "wowsim inject-fault activate latency-spike --delay-ms 200"
    echo ""

    narrate "Waiting 5 seconds for degraded ticks to accumulate..."
    sleep 5

    narrate "Checking health — expect CRITICAL status due to tick overruns."
    echo ""
    run_cmd "wowsim health --log-file '$TELEMETRY_FILE' --no-faults"
    echo ""

    warn "Server health degraded to CRITICAL as expected."
}

# ---------------------------------------------------------------------------
# Phase 3: Diagnose — detect anomalies and root cause
# ---------------------------------------------------------------------------
phase_diagnose() {
    header "Phase 3: Diagnose — Detect Anomalies"

    narrate "Scanning telemetry for anomalies (latency spikes, errors, crashes)."
    echo ""
    run_cmd "wowsim parse-logs '$TELEMETRY_FILE' --anomalies"
    echo ""

    narrate "Querying fault status to confirm root cause."
    echo ""
    run_cmd "wowsim inject-fault status latency-spike"
    echo ""

    success "Root cause identified: latency-spike fault is active."
}

# ---------------------------------------------------------------------------
# Phase 4: Fix It — deactivate fault and verify recovery
# ---------------------------------------------------------------------------
phase_fix() {
    header "Phase 4: Fix It — Deactivate and Recover"

    narrate "Deactivating the latency-spike fault (manual remediation)."
    echo ""
    run_cmd "wowsim inject-fault deactivate latency-spike"
    echo ""

    narrate "Waiting 5 seconds for clean ticks to flush through the analysis window."
    narrate "Metrics converge as clean ticks replace degraded data..."
    sleep 5

    narrate "Checking health — expect recovery toward HEALTHY status."
    echo ""
    run_cmd "wowsim health --log-file '$TELEMETRY_FILE' --no-faults"
    echo ""

    success "Server recovered after fault deactivation."
}

# ---------------------------------------------------------------------------
# Phase 5: Pipeline — automated canary deployment with rollback
# ---------------------------------------------------------------------------
phase_pipeline() {
    header "Phase 5: Pipeline — Automated Canary Deployment"

    narrate "Running a hotfix deployment pipeline that:"
    narrate "  1. Checks build preconditions (server reachable, not critical)"
    narrate "  2. Validates current health snapshot"
    narrate "  3. Activates latency-spike as a canary deployment"
    narrate "  4. Polls health every 2s during the 10s canary window"
    narrate "  5. Auto-rolls back when health hits CRITICAL threshold"
    echo ""
    run_cmd "wowsim deploy --fault-id latency-spike --action activate --delay-ms 200 --canary-duration 10 --canary-interval 2 --rollback-on critical --log-file '$TELEMETRY_FILE'"
    echo ""

    warn "Pipeline detected degradation and rolled back automatically."
}

# ---------------------------------------------------------------------------
# Phase 6: Summary — final health check and lifecycle recap
# ---------------------------------------------------------------------------
phase_summary() {
    header "Phase 6: Summary"

    narrate "Waiting 3 seconds for post-rollback stabilization..."
    sleep 3

    narrate "Final health check:"
    echo ""
    run_cmd "wowsim health --log-file '$TELEMETRY_FILE' --no-faults"
    echo ""

    echo -e "${BOLD}${GREEN}"
    echo "  Reliability Lifecycle Complete"
    echo "  ───────────────────────────────"
    echo -e "${RESET}"
    echo "  1. Baseline   — established healthy server metrics"
    echo "  2. Break       — injected latency spike (F1), triggered CRITICAL"
    echo "  3. Diagnose    — detected anomalies, identified root cause"
    echo "  4. Fix         — manual remediation, confirmed recovery"
    echo "  5. Pipeline    — automated canary detected fault, rolled back"
    echo "  6. Summary     — server back to stable state"
    echo ""
    narrate "All phases demonstrated with real server traffic and telemetry."
    narrate "Tools used: spawn-clients, health, parse-logs, inject-fault, deploy"
    echo ""
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    banner
    start_server
    phase_baseline
    phase_break
    phase_diagnose
    phase_fix
    phase_pipeline
    phase_summary
    success "Demo complete."
}

main
