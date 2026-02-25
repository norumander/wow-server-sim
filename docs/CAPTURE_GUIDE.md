# Screenshot & GIF Capture Guide

Step-by-step instructions for recording the visual assets referenced in `README.md`.

## Prerequisites

1. **Built server binary** — `bash scripts/build.sh`
2. **Python venv** — `bash scripts/setup_venv.sh && source .venv/bin/activate` (or `.venv/Scripts/activate` on Windows)
3. **Terminal setup:**
   - 120 columns minimum width
   - Dark background theme (black or dark grey)
   - 14pt monospace font (JetBrains Mono, Fira Code, or similar)
   - Disable terminal bell / flash

## Recommended Tools

| Tool | Purpose | Install |
|------|---------|---------|
| [asciinema](https://asciinema.org/) | Record terminal sessions | `pip install asciinema` |
| [agg](https://github.com/asciinema/agg) | Convert `.cast` to GIF | `cargo install agg` or download binary |
| [gifsicle](https://www.lcdf.org/gifsicle/) | Optimize GIF file size | `apt install gifsicle` / `brew install gifsicle` |
| [pngquant](https://pngquant.org/) | Optimize PNG file size | `apt install pngquant` / `brew install pngquant` |

For PNGs, your OS screenshot tool works fine (Snipping Tool on Windows, `gnome-screenshot` on Linux, Cmd+Shift+4 on macOS).

## Asset Checklist

| # | Filename | Type | Target Size | README Section |
|---|----------|------|-------------|----------------|
| 1 | `docs/assets/demo-full.gif` | GIF | < 5 MB | Hero banner |
| 2 | `docs/assets/dashboard-tui.png` | PNG | < 500 KB | Architecture |
| 3 | `docs/assets/demo-baseline.png` | PNG | < 500 KB | Demo |
| 4 | `docs/assets/demo-fault-injection.png` | PNG | < 500 KB | Demo |
| 5 | `docs/assets/demo-pipeline-rollback.png` | PNG | < 500 KB | Demo |
| 6 | `docs/assets/fault-cascade.gif` | GIF | < 5 MB | Fault Scenarios |

## Capture Instructions

### 1. `demo-full.gif` — Full Demo Walkthrough

The hero GIF showing the complete 70-second demo sped up.

```bash
# Start recording
asciinema rec demo-full.cast --cols 120 --rows 35

# Inside the recording, run:
bash scripts/demo.sh

# Stop recording (Ctrl+D or exit)

# Convert to GIF at 3x speed
agg demo-full.cast docs/assets/demo-full.gif --speed 3 --font-size 14

# Optimize (target < 5MB)
gifsicle -O3 --lossy=80 docs/assets/demo-full.gif -o docs/assets/demo-full.gif
```

**Verify:** GIF shows all 6 phases with colored output, health reports, and pipeline results visible.

### 2. `docs/assets/dashboard-tui.png` — Monitoring Dashboard

The Textual TUI with live server data.

```bash
# Terminal 1: Start the server
./build/wow-server-sim

# Terminal 2: Generate traffic
source .venv/bin/activate
wowsim spawn-clients --count 10 --duration 120 --rate 2

# Terminal 3: Launch dashboard
source .venv/bin/activate
wowsim dashboard --log-file telemetry.jsonl

# Wait ~10 seconds for metrics to populate, then screenshot Terminal 3
```

**Verify:** Screenshot shows the status bar (HEALTHY), tick metrics panel with non-zero values, zone health table with 7 columns (Zone, State, Ticks, Errors, Avg ms, Casts, DPS) with both zones ACTIVE and non-zero Casts/DPS values, game mechanics panel with cast stats and threat table, and scrolling event log with recent entries.

**Optimize:**
```bash
pngquant --quality=65-80 docs/assets/dashboard-tui.png --output docs/assets/dashboard-tui.png --force
```

### 3. `docs/assets/demo-baseline.png` — Healthy Baseline

Phase 1 of the demo showing a healthy server.

```bash
# Start the server and spawn clients
./build/wow-server-sim &
source .venv/bin/activate
wowsim spawn-clients --count 5 --duration 30 --rate 2 &
sleep 5

# Run health check
wowsim health --log-file telemetry.jsonl --no-faults

# Screenshot the terminal showing the HEALTHY health report
```

**Verify:** Output shows `Status: HEALTHY`, non-zero tick count, both zones ACTIVE, 5 connected players, no anomalies.

### 4. `docs/assets/demo-fault-injection.png` — Fault Injection (CRITICAL)

Phases 2-3 of the demo with an active fault and detected anomalies.

```bash
# With server and clients still running from step 3:

# Inject latency spike
wowsim inject-fault activate latency_spike --delay-ms 200

# Wait for a few ticks
sleep 3

# Run health check + anomaly scan
wowsim health --log-file telemetry.jsonl --no-faults
wowsim parse-logs telemetry.jsonl --anomalies

# Screenshot showing CRITICAL health report and anomaly list
```

**Verify:** Output shows `Status: CRITICAL`, tick overruns > 0%, and anomaly list with `[CRITICAL] latency_spike` entries.

### 5. `docs/assets/demo-pipeline-rollback.png` — Pipeline Rollback

Phase 5 of the demo showing a canary deployment that triggers rollback.

```bash
# With the latency fault still active:

# Run the deploy pipeline (will detect degradation and rollback)
wowsim deploy --fault-id latency_spike --action activate \
  --canary-duration 5 --canary-interval 1 --rollback-on degraded \
  --log-file telemetry.jsonl

# Screenshot the pipeline report showing ROLLED_BACK outcome
```

**Verify:** Output shows `=== Hotfix Pipeline Report ===` with build/validate PASS, canary FAIL, rollback PASS, and `Outcome: ROLLED_BACK`.

### 6. `docs/assets/fault-cascade.gif` — Cascading Zone Failure (Optional)

Advanced fault scenario F5 showing cross-zone failure propagation.

```bash
# Start recording
asciinema rec fault-cascade.cast --cols 120 --rows 35

# Start server + clients
./build/wow-server-sim &
source .venv/bin/activate
wowsim spawn-clients --count 20 --duration 60 --rate 2 &
sleep 5

# Show healthy baseline
wowsim health --log-file telemetry.jsonl --no-faults

# Inject cascading failure
wowsim inject-fault activate cascading_zone_failure --zone 1

# Watch health degrade
sleep 3
wowsim health --log-file telemetry.jsonl --no-faults
wowsim parse-logs telemetry.jsonl --anomalies

# Recover
wowsim inject-fault deactivate cascading_zone_failure
sleep 3
wowsim health --log-file telemetry.jsonl --no-faults

# Stop recording (Ctrl+D)

# Convert to GIF
agg fault-cascade.cast docs/assets/fault-cascade.gif --speed 2 --font-size 14
gifsicle -O3 --lossy=80 docs/assets/fault-cascade.gif -o docs/assets/fault-cascade.gif
```

**Verify:** GIF shows healthy → zone 1 crash → zone 2 overload → recovery arc.

## Docker-Based Recording (Recommended)

The easiest way to produce the two GIF assets is to use the recording Docker container, which bundles asciinema, agg, and gifsicle — no host-side tooling required.

### Quick Start

```bash
# Build the recording image and run both recordings
docker compose --profile record run record

# GIFs land directly in docs/assets/ via volume mount
ls -lh docs/assets/demo-full.gif docs/assets/fault-cascade.gif
```

### How It Works

`Dockerfile.recording` extends the main multi-stage build:
- Adds `curl`, `gifsicle` via apt
- Installs `asciinema` into the existing Python venv (avoids PEP 668 externally-managed issues)
- Downloads `agg` as a static binary from GitHub releases

Three scripts orchestrate the recordings:

| Script | Purpose |
|--------|---------|
| `scripts/record-gifs.sh` | Master script — runs both recordings sequentially, reports results |
| `scripts/record-demo-full.sh` | Records `demo-full.gif` — full 70s demo at 3x speed |
| `scripts/record-fault-cascade.sh` | Records `fault-cascade.gif` — F5 cascading zone failure at 2x speed |

### Recording Parameters

| Parameter | demo-full | fault-cascade |
|-----------|-----------|---------------|
| Terminal size | 120x35 | 120x35 |
| Playback speed | 3x | 2x |
| Font size | 14 | 14 |
| gifsicle lossy | 80 | 80 |
| Target size | < 5 MB | < 5 MB |

### Customization

To adjust recording parameters, edit the variables at the top of each `record-*.sh` script:

```bash
COLS=120      # Terminal columns
ROWS=35       # Terminal rows
SPEED=3       # Playback speed multiplier
FONT_SIZE=14  # GIF font size
LOSSY=80      # gifsicle lossy compression level (0-200)
```

### Troubleshooting

- **Build fails on ARM/Apple Silicon**: `agg` is downloaded as an x86_64 binary. The container must run under `linux/amd64` (Docker Desktop emulates this by default).
- **GIF too large**: Increase `SPEED` or `LOSSY` value in the recording script.
- **Recording hangs**: The server binary must start within 10 seconds. If the image is stale, rebuild with `docker compose --profile record build record`.

## General Tips

- **Consistent terminal size**: Use 120x35 for all captures so they look uniform in the README.
- **Clean state**: Kill any running server and delete `telemetry.jsonl` before each capture session for clean data.
- **GIF file size**: If a GIF exceeds 5MB after optimization, increase `--speed` or reduce `--rows`.
- **PNG file size**: If a PNG exceeds 500KB after pngquant, crop to just the terminal window (no desktop background).
- **Dark theme**: All captures should use a dark terminal theme for consistency and readability on GitHub's dark mode.
