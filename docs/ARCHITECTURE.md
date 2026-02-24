# Architecture — WoW Server Simulator

> **Status:** Living document. Updated with every component addition or modification.

## High-Level Overview

The WoW Server Simulator consists of a C++17 game server and a Python tooling suite. The server handles game simulation (tick loop, sessions, combat, spells, zones) while Python tools provide observability, fault injection, and traffic generation.

```
┌─────────────────────────────────────────────────────────────────┐
│                        Docker Container                         │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    C++ Game Server                         │  │
│  │                                                           │  │
│  │  ┌─────────────┐    ┌──────────────────────────────────┐  │  │
│  │  │   Network    │    │          ZoneManager              │  │  │
│  │  │   Thread     │    │  ┌────────┐ ┌────────┐ ┌──────┐  │  │  │
│  │  │             │───▶│  │ Zone 1 │ │ Zone 2 │ │Zone N│  │  │  │
│  │  │  TCP Accept  │    │  │        │ │        │ │      │  │  │  │
│  │  │  Read/Write  │    │  │ Tick   │ │ Tick   │ │ Tick │  │  │  │
│  │  └─────────────┘    │  │Pipeline│ │Pipeline│ │Pline │  │  │  │
│  │        │             │  └────────┘ └────────┘ └──────┘  │  │  │
│  │        │             └──────────────────────────────────┘  │  │
│  │        │                           │                       │  │
│  │   ┌────▼────┐              ┌───────▼──────┐               │  │
│  │   │ Intake  │              │  Telemetry   │               │  │
│  │   │ Queue   │              │  Emitter     │               │  │
│  │   └─────────┘              │ ┌──────────┐ │               │  │
│  │                            │ │ Log File │ │               │  │
│  │  ┌─────────────┐          │ │ (durable)│ │               │  │
│  │  │  Control    │          │ ├──────────┤ │               │  │
│  │  │  Channel    │◀── TCP   │ │   UDP    │ │               │  │
│  │  │  (JSON/TCP) │          │ │(realtime)│ │               │  │
│  │  └─────────────┘          │ └──────────┘ │               │  │
│  │        ▲                   └──────────────┘               │  │
│  └────────│──────────────────────────│───────────────────────┘  │
└───────────│──────────────────────────│──────────────────────────┘
            │                          │
    ┌───────┴────────┐     ┌───────────▼───────────┐
    │   wowsim CLI   │     │   wowsim dashboard    │
    │                │     │     (Textual TUI)      │
    │ • inject-fault │     │                        │
    │ • health       │     │  Metrics │ Zone Table  │
    │ • parse-logs   │     │  ────────┼───────────  │
    │ • spawn-clients│     │  Event Log (scrolling) │
    └────────────────┘     └────────────────────────┘
         Host Python              Host Python
```

## Component Descriptions

### Network Thread
- **Responsibility:** TCP accept, read/write for game clients and control channel
- **Library:** Standalone Asio (non-Boost), header-only via CMake FetchContent
- **Pattern:** Async I/O — Asio abstracts epoll/IOCP, non-blocking I/O, buffer management
- **Ownership:** Does NOT own game state. Enqueues events to intake queue only.

### Game Loop Thread (`src/server/game_loop.h`)
- **Implementation:** `wow::GameLoop` class with configurable tick rate via `GameLoopConfig` (default 20 Hz / 50ms, matching WoW's actual server tick rate)
- **Timing strategy:** `steady_clock` + sleep-for-remainder. After executing callbacks, sleeps for `tick_interval - elapsed`. On overrun (work exceeds tick interval), skips sleep and continues immediately — no debt accumulation
- **Thread model:** Background thread via `start()`/`stop()`, or blocking via `run()`. Destructor calls `stop()` for RAII thread ownership
- **Callbacks:** `on_tick(std::function<void(uint64_t)>)` registers callbacks invoked each tick with a sequential 0-indexed tick number. Registered before `start()` (not thread-safe by design)
- **Thread safety:** `std::atomic<bool> running_` and `std::atomic<uint64_t> tick_count_` for cross-thread reads. `compare_exchange_strong` guards start/stop transitions
- **Telemetry:** Emits `"Game loop started"` event (with `tick_rate_hz`, `tick_interval_ms`), per-tick metric (with `tick`, `duration_ms`, `overrun` flag), and `"Game loop stopped"` event (with `total_ticks`)
- **Pattern:** Producer/consumer with network thread via thread-safe intake queue
- **MVP:** Single thread calls `zone.tick()` sequentially for each zone (zone callbacks registered via `on_tick`)
- **Polish:** Migrates to thread-per-zone, each zone gets its own tick loop
- **Test strategy:** 20 GoogleTest cases covering construction/config, start/stop lifecycle, tick execution, telemetry emission, overrun detection, and timing accuracy. Tests use high tick rates (100–1000 Hz) with generous bounds for CI scheduler variance

### Zone (Self-Contained Processing Unit)
- **Responsibility:** Owns its session registry, entity list, event queue, tick pipeline
- **Isolation:** Exception guard around `zone.tick()` — crash in one zone cannot propagate
- **Health:** Reports metrics after each tick (duration, event backlog, error count)

### Tick Pipeline (Per Zone, Per Tick)
```
FaultPreTickPhase       ← tick-scoped fault interceptors
DrainIntakePhase        ← distribute events from intake → zone queue
MovementPhase           ← process position updates, cancel casts
SpellCastPhase          ← advance cast timers, resolve completions
CombatPhase             ← calculate damage (armor/resistance), apply
ThreatUpdatePhase       ← update threat tables, resolve retargets
SessionSweepPhase       ← mark-and-sweep disconnected sessions
TelemetryEmitPhase      ← emit zone health metrics (file + UDP)
FaultPostTickPhase      ← tick-scoped fault interceptors
```

### Session State Machine
States: `CONNECTING → AUTHENTICATING → IN_WORLD → TRANSFERRING → DISCONNECTING → DESTROYED`

Implemented as a transition table — central table of `{from_state, event, to_state, action}` tuples. All transitions flow through one `transition()` function with telemetry logging. Invalid transitions are rejected and logged.

### Telemetry (`src/server/telemetry/logger.h`)
- **Implementation:** `wow::Logger` singleton, initialized via `Logger::initialize(config)`, accessed via `Logger::instance()`
- **Format:** JSON Lines with schema versioning (`"v": 1`). Each line is a self-contained JSON object with fields: `v`, `timestamp`, `type`, `component`, `message`, and optional `data`
- **Timestamp:** ISO 8601 with millisecond precision (`YYYY-MM-DDTHH:MM:SS.mmmZ`), UTC via `gmtime_r`/`gmtime_s`
- **Types:** `"metric"`, `"event"`, `"health"`, `"error"` — mapped from `wow::LogType` enum
- **Sinks:** Configurable via `LoggerConfig` — file (append mode, flushed per line for crash durability), stdout (Docker-friendly), custom `std::ostream*` (test injection). UDP broadcast deferred until networking is in place (ADR-004)
- **Thread safety:** `std::mutex` guards sink writes; `format_entry()` runs lock-free (reads immutable config, uses thread-safe time functions)
- **Convenience API:** `metric()`, `event()`, `health()`, `error()` wrappers delegate to `log()` with the appropriate `LogType`
- **Test strategy:** 28 GoogleTest cases using `std::ostringstream` as custom sink — covers schema compliance, type mapping, data handling, multi-line output, file I/O, and concurrent writes

### Control Channel
- **Protocol:** TCP with newline-delimited JSON
- **Purpose:** Runtime fault injection without disrupting game loop
- **Interface:** Separate port from game traffic

### Fault Injection
- **Architecture:** `FaultRegistry` with self-registration pattern
- **Tick-scoped faults:** Fire at hook points in tick pipeline (latency spike, event flood, session crash)
- **Ambient faults:** Run independently when activated (memory pressure, slow leak)

### Python Tooling (`wowsim` CLI)
- **Package:** Installable via `pip install -e tools/`, provides `wowsim` command
- **Shared modules:** `protocol.py`, `telemetry.py`, `models.py`, `config.py`
- **Async strategy:** Async core, sync CLI surface via `asyncio.run()`

## Concurrency Model

**MVP (Two Threads):**
- Network thread: Asio io_context, handles all TCP I/O
- Game loop thread: Fixed-rate tick, processes all zones sequentially
- Communication: Thread-safe intake queue (mutex + condition variable)
- Ownership boundary: Game thread owns all game state. Network thread only enqueues.

**Polish (Thread-per-Zone):**
- Each zone gets its own thread and tick loop
- ZoneManager becomes a supervisor
- Per-zone queues replace single intake queue
- No fundamental architectural change — queue interface stays the same

## Key Design Patterns
- **Producer/Consumer:** Network → intake queue → game loop
- **Two-Stage Queue:** Intake → per-zone distribution
- **Transition Table:** Session state machine
- **Phase Pipeline:** Ordered tick processing
- **Mark-and-Sweep:** Session cleanup with reconnection window
- **Exception Guard:** Zone-level fault isolation
- **Self-Registration:** Fault registry pattern
