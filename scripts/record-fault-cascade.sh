#!/usr/bin/env bash
# record-fault-cascade.sh — Record the F5 cascading zone failure scenario as a GIF.
#
# Produces /output/fault-cascade.gif via asciinema + agg + gifsicle.
# Intended to run inside the recording Docker container.
set -euo pipefail

WORK_DIR="/tmp/recording"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
CAST_FILE="$WORK_DIR/fault-cascade.cast"
RAW_GIF="$WORK_DIR/fault-cascade-raw.gif"
FINAL_GIF="$OUTPUT_DIR/fault-cascade.gif"

COLS=120
ROWS=35
SPEED=2
FONT_SIZE=14
LOSSY=80

TELEMETRY_FILE="/app/telemetry.jsonl"
SERVER_BIN="/app/wow-server-sim"
SERVER_PID=""

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"

# ---------------------------------------------------------------------------
# Server lifecycle helpers
# ---------------------------------------------------------------------------
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

start_server() {
    rm -f "$TELEMETRY_FILE"
    "$SERVER_BIN" > /dev/null 2>&1 &
    SERVER_PID=$!

    # Wait for telemetry file to appear (server is ready)
    local elapsed=0
    while [ ! -f "$TELEMETRY_FILE" ]; do
        sleep 0.5
        elapsed=$((elapsed + 1))
        if [ $elapsed -ge 20 ]; then
            echo "ERROR: Server did not produce telemetry within 10 seconds."
            exit 1
        fi
    done
    sleep 2
}

# ---------------------------------------------------------------------------
# Generate a standalone scenario script for asciinema to execute.
# This avoids export -f issues across subprocess boundaries.
# ---------------------------------------------------------------------------
SCENARIO_SCRIPT="$WORK_DIR/cascade-inner.sh"

cat > "$SCENARIO_SCRIPT" << 'SCENARIO_EOF'
#!/usr/bin/env bash
set -euo pipefail

TELEMETRY_FILE="/app/telemetry.jsonl"

# Color utilities
BOLD='\033[1m'
DIM='\033[2m'
CYAN='\033[36m'
GREEN='\033[32m'
YELLOW='\033[33m'
RED='\033[31m'
MAGENTA='\033[35m'
RESET='\033[0m'

header()  { echo -e "\n${BOLD}${CYAN}=== $1 ===${RESET}\n"; }
narrate() { echo -e "${DIM}$1${RESET}"; }
success() { echo -e "${GREEN}[OK]${RESET} $1"; }
warn()    { echo -e "${YELLOW}[!!]${RESET} $1"; }
run_cmd() { echo -e "${MAGENTA}\$ $1${RESET}"; eval "$1"; }

echo -e "${BOLD}${CYAN}"
echo "  ╔══════════════════════════════════════════════════════╗"
echo "  ║  F5: Cascading Zone Failure — Reliability Scenario   ║"
echo "  ╚══════════════════════════════════════════════════════╝"
echo -e "${RESET}"

# Phase 1: Healthy baseline
header "Phase 1: Healthy Baseline"
narrate "Spawning 20 simulated players across two zones..."
run_cmd "wowsim spawn-clients --count 20 --duration 5"
echo ""
narrate "Checking server health:"
run_cmd "wowsim health --log-file '$TELEMETRY_FILE' --no-faults"
success "Both zones healthy under normal load."

# Phase 2: Inject cascading zone failure
header "Phase 2: Inject Cascading Zone Failure (F5)"
narrate "F5 crashes zone 1 and floods zone 2 with redirected traffic."
narrate "This simulates a real-world cascading failure pattern."
echo ""
run_cmd "wowsim inject-fault activate cascading-zone-failure --zone 1"
echo ""
narrate "Waiting 5 seconds for failure to propagate..."
sleep 5

# Phase 3: Observe degradation
header "Phase 3: Observe Degradation"
narrate "Checking health — expect zone 1 crashed, zone 2 overloaded:"
run_cmd "wowsim health --log-file '$TELEMETRY_FILE' --no-faults"
echo ""
narrate "Scanning for anomalies:"
run_cmd "wowsim parse-logs '$TELEMETRY_FILE' --anomalies"
warn "Cascading failure detected across zones."

# Phase 4: Recovery
header "Phase 4: Recovery"
narrate "Deactivating cascading-zone-failure fault..."
run_cmd "wowsim inject-fault deactivate cascading-zone-failure"
echo ""
narrate "Waiting 5 seconds for zones to recover..."
sleep 5
narrate "Final health check:"
run_cmd "wowsim health --log-file '$TELEMETRY_FILE' --no-faults"
echo ""
success "Zones recovered. Cascading failure scenario complete."
SCENARIO_EOF

chmod +x "$SCENARIO_SCRIPT"

# ---------------------------------------------------------------------------
# Main — start server, record scenario, convert to GIF
# ---------------------------------------------------------------------------
echo "=== Recording fault-cascade.gif ==="
echo "    Terminal: ${COLS}x${ROWS}"
echo "    Playback speed: ${SPEED}x"

start_server

# Record with asciinema
export ASCIINEMA_REC=1
asciinema rec "$CAST_FILE" \
    --cols "$COLS" \
    --rows "$ROWS" \
    --overwrite \
    --command "bash $SCENARIO_SCRIPT"

echo "    Cast file: $(du -h "$CAST_FILE" | cut -f1)"

# Convert .cast to GIF
echo "    Converting to GIF (speed=${SPEED}x, font-size=${FONT_SIZE})..."
agg "$CAST_FILE" "$RAW_GIF" \
    --speed "$SPEED" \
    --font-size "$FONT_SIZE" \
    --font-family "DejaVu Sans Mono"

echo "    Raw GIF: $(du -h "$RAW_GIF" | cut -f1)"

# Optimize GIF file size
echo "    Optimizing with gifsicle (lossy=${LOSSY})..."
gifsicle -O3 --lossy="$LOSSY" "$RAW_GIF" -o "$FINAL_GIF"

echo "    Final GIF: $(du -h "$FINAL_GIF" | cut -f1)"
echo "=== fault-cascade.gif complete ==="
