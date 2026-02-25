#!/usr/bin/env bash
# record-gifs.sh — Master script that runs all GIF recordings sequentially.
#
# Intended to run inside the recording Docker container:
#   docker compose --profile record run record
#
# Output lands in /output/ (volume-mounted to docs/assets/).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║          WoW Server Simulator — GIF Recorder             ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "Output directory: ${OUTPUT_DIR:-/output}"
echo ""

FAILED=0

# --- Recording 1: demo-full.gif ---
echo "────────────────────────────────────────────────────────────"
echo " [1/2] demo-full.gif"
echo "────────────────────────────────────────────────────────────"
if bash "$SCRIPT_DIR/record-demo-full.sh"; then
    echo ""
else
    echo "WARNING: record-demo-full.sh failed (exit $?)"
    FAILED=$((FAILED + 1))
fi

# --- Recording 2: fault-cascade.gif ---
echo ""
echo "────────────────────────────────────────────────────────────"
echo " [2/2] fault-cascade.gif"
echo "────────────────────────────────────────────────────────────"
if bash "$SCRIPT_DIR/record-fault-cascade.sh"; then
    echo ""
else
    echo "WARNING: record-fault-cascade.sh failed (exit $?)"
    FAILED=$((FAILED + 1))
fi

# --- Summary ---
echo ""
echo "════════════════════════════════════════════════════════════"
echo " Recording Summary"
echo "════════════════════════════════════════════════════════════"

OUTPUT_DIR="${OUTPUT_DIR:-/output}"

for gif in "$OUTPUT_DIR/demo-full.gif" "$OUTPUT_DIR/fault-cascade.gif"; do
    name="$(basename "$gif")"
    if [ -f "$gif" ]; then
        size="$(du -h "$gif" | cut -f1)"
        echo "  OK  $name ($size)"
    else
        echo "  MISSING  $name"
    fi
done

echo ""
if [ $FAILED -eq 0 ]; then
    echo "All recordings completed successfully."
else
    echo "WARNING: $FAILED recording(s) failed."
    exit 1
fi
