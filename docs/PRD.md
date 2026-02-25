# WoW Server Simulator — Product Requirements Document

**Application for:** Software Engineer, Server — World of Warcraft (R026699)

## Overview

WoW Server Simulator is a focused demo project that demonstrates the core competencies required for the WoW Server Reliability Engineer role. It combines a C++ game server implementation with Python reliability tooling to simulate the real-world challenges of keeping World of Warcraft running smoothly for millions of players.

The project is structured in two phases: a 24-hour MVP that proves core technical capability, followed by a week of polish that adds production-grade reliability features. Every design decision maps directly to the responsibilities listed in the job description.

## Problem Statement

A WoW server reliability engineer must simultaneously understand low-level C++ server code, diagnose issues under pressure using logs and telemetry, shepherd hotfixes safely to production, and maintain deep knowledge of game systems. This project creates a controlled environment that exercises all of these skills in a way that can be demonstrated, tested, and extended.

## Goals & Skills Mapping

Every feature in this project maps directly to a requirement in the job posting:

| Job Requirement | Project Demonstration | Specific Evidence |
|---|---|---|
| C++ and Python fluency | Core server in C++, all tooling in Python | Modern C++17, clean architecture, idiomatic Python CLI tools |
| Strong debugging skills | Fault injection + diagnosis workflow | Deliberately broken scenarios with log-driven root cause analysis |
| Networking / distributed systems | Multi-client TCP server with session management | Socket programming, concurrent connections, state synchronization |
| Reliability mindset | Health checks, automated rollback, failure prediction | Canary deployments, circuit breakers, graceful degradation |
| Shepherd hotfixes dev to live | Simulated deployment pipeline | Multi-stage rollout with automated health validation |
| WoW domain knowledge | Authentic game mechanics simulation | Spell casts, combat ticks, movement, zone transitions modeled on real WoW systems |
| Automation to reduce toil | Python tooling suite | Automated log analysis, alert generation, deployment orchestration |

## Architecture

| Component | Language | Responsibility |
|---|---|---|
| Game Server | C++17 | Main game loop, player session management, event processing, combat/movement simulation, fault injection hooks |
| Telemetry Emitter | C++ (integrated) | Structured logging, performance counters, health heartbeats written to log files and stdout |
| CLI Toolkit | Python 3.11+ | Log analysis, fault injection triggers, server health reports, hotfix deployment simulation |
| Monitoring Dashboard | Python (curses / Flask) | Real-time visualization of server metrics, alert thresholds, event throughput graphs |
| Test Harness | C++ / Python | GoogleTest for C++ unit tests, pytest for integration and end-to-end failure scenario tests |

## Data Flow

The system follows a straightforward pipeline: mock game clients connect to the C++ server over TCP. The server processes game events on a fixed tick rate (similar to WoW's server tick), emits structured telemetry to log files, and exposes health endpoints. Python tooling reads these logs in real-time for analysis, alerting, and visualization. The fault injector communicates with the server via a separate control channel to trigger failure scenarios without disrupting the main game loop.

## MVP Scope (24 Hours)

### C++ Game Server Core

- TCP socket server accepting multiple concurrent mock player connections
- Fixed-rate game tick loop (configurable, default ~20 ticks/sec matching WoW's server tick)
- Player session lifecycle: connect, authenticate (mock), enter world, disconnect
- Basic game event processing: movement updates, spell cast initiation, combat tick damage calculation
- Structured telemetry output: JSON-formatted log lines with timestamps, event types, session IDs, and performance counters
- Fault injection hooks: compile-time flags and runtime triggers for introducing latency spikes, memory pressure, crash scenarios, and event processing failures

### WoW-Authentic Game Mechanics

Modeled on real WoW server behavior to demonstrate domain expertise:

- Spell cast system with cast times, GCD (Global Cooldown), and interrupt handling
- Combat tick processing with damage/healing calculations on server tick boundaries
- Zone/instance concept with player-to-zone assignment and zone-level failure isolation
- Basic threat/aggro table for NPC interactions

### Python CLI Toolkit

- Log parser that reads structured server telemetry and surfaces anomalies (latency spikes, error rates, dropped connections)
- Fault injection CLI: trigger specific failure modes on the running server via the control channel
- Health check reporter: periodic server health summary including player counts, tick rate stability, memory usage, event throughput
- Mock client spawner: spin up N simulated players that generate realistic game traffic patterns

### Test Coverage

- GoogleTest suite for C++ server logic: event processing, session management, combat calculations
- Pytest integration tests: client connection lifecycle, fault injection and recovery, log output validation

## Polish Scope (1 Week)

### Monitoring Dashboard
- Real-time terminal UI or web dashboard showing server metrics
- Tick rate stability graph, player count per zone, event throughput, error rate
- Alert thresholds with visual indicators when metrics exceed bounds
- Fault injection event timeline

### Hotfix Pipeline Simulation
- Simulated build-validate-canary-promote-rollback pipeline
- Automated health checks at each stage
- Rollback triggers based on telemetry thresholds

### Advanced Failure Scenarios
- Cascading zone failure (zone 1 crash redistributes to zone 2, overloading it)
- Slow memory leak detection
- Split-brain zone partition
- Thundering herd reconnection storm

### Performance & Benchmarking
- Max concurrent player benchmarks
- Tick stability under load testing
- Telemetry overhead measurement

## Phase 3 Scope: Game Mechanics Visibility

Phase 2 revealed a critical gap: the server implements rich WoW game mechanics (spell casting, combat, movement) that are fully tested but invisible in the live demo and tooling. `Connection::do_read()` discards all TCP data from mock clients. Phase 3 closes this gap.

### TCP Event Parsing (Milestone 1)
- Replace the discard loop in `Connection::do_read()` with newline-delimited JSON parsing
- Deserialize incoming payloads into `GameEvent` objects (movement, spell_cast, combat)
- Route parsed events to the correct zone's `EventQueue` via `ZoneManager`
- Graceful error handling: malformed JSON is logged and dropped, never crashes the connection

### Game-Mechanic Telemetry in Tooling (Milestone 2)
- Extend log_parser with game-mechanic aggregation: cast rates, GCD block rates, DPS, cast success rate
- Extend health_check with game-mechanic status signals: high GCD block rate → degraded, zero combat → warning
- Add Game Mechanics panel to the dashboard TUI

### Demo Narrative Evolution (Milestone 3)
- Rewrite demo.sh to showcase WoW-aware SRE: "spells fail under load → game-mechanic-aware detection → fix → canary validates cast success recovery"
- Demo proves both infrastructure reliability AND WoW domain knowledge

### Dashboard Polish (Milestone 4)
- Per-zone cast/DPS columns, threat table summary view
- Updated screenshots/GIFs for README reflecting game mechanic panels

## Success Criteria

1. Server handles 50+ concurrent mock players without tick rate degradation
2. All fault scenarios produce detectable telemetry signatures
3. Python tools correctly identify and surface all injected faults
4. Full demo walkthrough runs end-to-end in under 5 minutes
5. All tests pass in CI (GitHub Actions)
6. Documentation is complete and accurate
7. Mock client traffic produces game-mechanic telemetry (spell casts, combat, movement) in live server
8. Python tooling surfaces game-mechanic metrics (cast rate, DPS, GCD blocks) alongside infrastructure metrics
9. Demo narrative showcases WoW-specific failure modes and their player impact
10. Dashboard screenshot in README shows game-mechanic panels
