#!/usr/bin/env bash
# record-demo-full.sh — Record the full 70s demo walkthrough as a GIF.
#
# Produces /output/demo-full.gif via asciinema + agg + gifsicle.
# Intended to run inside the recording Docker container.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="/tmp/recording"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
CAST_FILE="$WORK_DIR/demo-full.cast"
RAW_GIF="$WORK_DIR/demo-full-raw.gif"
FINAL_GIF="$OUTPUT_DIR/demo-full.gif"

COLS=120
ROWS=35
SPEED=3
FONT_SIZE=14
LOSSY=80

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"

echo "=== Recording demo-full.gif ==="
echo "    Terminal: ${COLS}x${ROWS}"
echo "    Playback speed: ${SPEED}x"

# Clean telemetry state for a fresh recording
rm -f /app/telemetry.jsonl

# Record the full demo script with asciinema.
# --overwrite: replace any previous .cast file
# ASCIINEMA_REC=1 signals non-interactive recording mode
export ASCIINEMA_REC=1
asciinema rec "$CAST_FILE" \
    --cols "$COLS" \
    --rows "$ROWS" \
    --overwrite \
    --command "bash $SCRIPT_DIR/demo.sh"

echo "    Cast file: $(du -h "$CAST_FILE" | cut -f1)"

# Convert .cast to GIF
echo "    Converting to GIF (speed=${SPEED}x, font-size=${FONT_SIZE})..."
agg "$CAST_FILE" "$RAW_GIF" \
    --speed "$SPEED" \
    --font-size "$FONT_SIZE"

echo "    Raw GIF: $(du -h "$RAW_GIF" | cut -f1)"

# Optimize GIF file size
echo "    Optimizing with gifsicle (lossy=${LOSSY})..."
gifsicle -O3 --lossy="$LOSSY" "$RAW_GIF" -o "$FINAL_GIF"

echo "    Final GIF: $(du -h "$FINAL_GIF" | cut -f1)"
echo "=== demo-full.gif complete ==="
