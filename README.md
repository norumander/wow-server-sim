# WoW Server Simulator

A C++17 game server with Python reliability tooling that demonstrates server reliability engineering skills for the World of Warcraft Server team at Blizzard Entertainment.

The project simulates a simplified WoW game server — fixed-rate tick loop, player sessions, combat, spells, zone isolation — then layers fault injection, telemetry, and automated recovery tooling on top, showcasing the full lifecycle of **detect, diagnose, fix, deploy**.

## Quick Start

### Docker (recommended)

```bash
git clone <repo-url> && cd wow-server-sim
docker compose up --build
```

The server exposes:
- **8080** — Game traffic (TCP)
- **8081** — Control channel for fault injection (TCP)
- **9090** — Telemetry broadcast (UDP)

### Python Tooling

```bash
bash scripts/setup_venv.sh
source .venv/bin/activate
wowsim --help
```

Available commands:
- `wowsim health` — Check server health
- `wowsim inject-fault` — Inject a fault scenario
- `wowsim parse-logs` — Analyze telemetry logs
- `wowsim spawn-clients` — Generate simulated player traffic
- `wowsim dashboard` — Launch the monitoring TUI

### Build From Source (Linux / Docker)

```bash
bash scripts/build.sh
./build/wow-server-sim
```

### Run Tests

```bash
# All tests (C++ + Python)
bash scripts/run_all_tests.sh

# C++ only
cd build && ctest --output-on-failure

# Python only
source .venv/bin/activate
python -m pytest tests/python/ -v
```

## Run Demo

The demo script walks through the full reliability lifecycle in ~70 seconds — no manual intervention required:

```bash
bash scripts/demo.sh
```

**What it does (6 phases):**
1. **Baseline** — spawns 5 simulated players, verifies healthy server
2. **Break** — injects a 200ms latency spike (4x tick budget overrun)
3. **Diagnose** — scans telemetry for anomalies, identifies root cause
4. **Fix** — deactivates the fault, verifies recovery
5. **Pipeline** — runs automated canary deployment with auto-rollback
6. **Summary** — final health check and lifecycle recap

**Prerequisites:** built server binary + Python venv with `wowsim` installed (see Quick Start above).

## Architecture

The server uses a two-thread model (network thread + game loop thread) with a fixed 20 Hz tick rate matching WoW's actual server tick rate. Zones are self-contained processing units with fault isolation — a crash in one zone cannot propagate to others.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for full component descriptions, data flow, and concurrency model.

## Documentation

- [Product Requirements](docs/PRD.md) — Full project scope and goals
- [Architecture](docs/ARCHITECTURE.md) — Component design, data flow, concurrency model
- [Decision Records](docs/DECISIONS.md) — ADR log for all architectural choices
- [Changelog](docs/CHANGELOG.md) — Running log of all changes

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Server | C++17, standalone Asio, nlohmann/json |
| Tooling | Python 3.11+, Click CLI, Textual TUI |
| Testing | GoogleTest (C++), pytest (Python) |
| Container | Docker, docker-compose |
| CI | GitHub Actions |

## Project Status

Phase 1 (MVP) is under active development. See the [PRD](docs/PRD.md) for the full implementation roadmap.
