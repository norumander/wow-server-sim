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

### Network Thread — GameServer (`src/server/game_server.h`)
- **Responsibility:** TCP accept, read/write for game clients and control channel
- **Library:** Standalone Asio (non-Boost), header-only via CMake FetchContent
- **Pattern:** Async I/O — Asio abstracts epoll/IOCP, non-blocking I/O, buffer management
- **Ownership:** Does NOT own game state. Enqueues events to intake queue only.
- **Implementation:** `wow::GameServer` class with `GameServerConfig` (port, default 8080). Runs `asio::io_context` on a dedicated `std::thread`. Async accept loop creates `shared_ptr<Connection>` per client. Thread-safe connection registry (`std::unordered_map<uint64_t, shared_ptr<Connection>>`) protected by `std::mutex`, with a separate `std::atomic<size_t>` for lock-free connection count reads
- **Lifecycle:** `start()` binds acceptor (port 0 = OS-assigned), begins async accept, spawns network thread. `stop()` closes acceptor, closes all connections, stops io_context, joins thread. Destructor calls `stop()` for RAII. Double-start and double-stop are no-ops via `compare_exchange_strong`
- **Port 0 support:** Tests use port 0 for OS-assigned ephemeral ports, retrieved via `acceptor_.local_endpoint().port()` after bind
- **Telemetry:** Emits `"Server started"` (with port), `"Server stopped"`, `"Connection accepted"` (with session_id, remote_endpoint), and `"Client disconnected"` (with session_id)

### Connection (`src/server/connection.h`)
- **Implementation:** `wow::Connection` bridges the network layer (TCP socket) and the game layer (Session). Each Connection owns a `Session` by value (starts in CONNECTING state) and uses `enable_shared_from_this` for safe async callback capture
- **Disconnect detection:** Async read loop via `async_read_some` — received data is discarded (no protocol parsing at this stage). On EOF/error, transitions session via `DISCONNECT` event (CONNECTING → DESTROYED) and invokes the disconnect callback
- **shared_from_this ordering:** `do_accept()` creates `make_shared<Connection>`, stores it in the connection map, THEN calls `conn->start()`. This ensures `shared_from_this()` works inside the async read loop
- **Scope boundary:** No message framing, no protocol parsing, no game logic. The read loop exists solely for disconnect detection
- **Test strategy:** 23 GoogleTest cases covering construction (3), start/stop lifecycle (5), connection acceptance (4), disconnect handling (4), telemetry emission (4), and edge cases (3). All tests use port 0 with a `wait_for` polling helper (10ms intervals, 500ms default timeout)

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

### Event System (`src/server/events/`)
- **Base class:** `wow::GameEvent` with `EventType` tag enum (`MOVEMENT`, `SPELL_CAST`, `COMBAT`) and `session_id`. Owned via `std::unique_ptr<GameEvent>` for single-ownership transfer through the event queue. Virtual destructor enables polymorphic deletion; type tag enables safe `static_cast` downcasting without RTTI overhead
- **Event types:** `wow::MovementEvent` carries a target `Position`. `wow::SpellCastEvent` carries `SpellAction` (CAST_START/INTERRUPT), `spell_id`, and `cast_time_ticks`. `COMBAT` type reserved for Step 8
- **String conversion:** `event_type_to_string()` free function returning `std::string_view`

### Event Queue (`src/server/events/event_queue.h`)
- **Implementation:** `wow::EventQueue` — thread-safe producer/consumer queue between network and game threads
- **Thread safety:** Single `std::mutex` protects all operations. `push()` appends to internal vector; `drain()` uses `swap()` for O(1) bulk transfer, returning all queued events and clearing the queue atomically
- **Telemetry accessors:** `size()` and `empty()` are mutex-protected for consistent reads
- **Ownership boundary:** Network thread pushes events; game thread drains at tick start. Entity map is NOT mutex-protected — owned exclusively by the game thread
- **Test strategy:** 4 GoogleTest cases covering empty drain, push/drain round-trip, drain-clears-queue semantics, and concurrent multi-thread push with single-thread drain

### Movement Processor (`src/server/events/movement.h`)
- **Implementation:** `wow::MovementProcessor::process()` filters events for `EventType::MOVEMENT`, downcasts via `static_cast<MovementEvent*>` (safe: type tag guarantees correct cast), looks up entity by `session_id`, and updates position
- **Unknown sessions:** Skipped with error telemetry — no entity created, no crash
- **Last-wins semantics:** Multiple movement events for the same session in one tick: processor iterates in order, last position overwrites earlier ones (matches WoW server behavior)
- **Cross-phase flag:** Sets `entity.cast_state().moved_this_tick = true` on position update. SpellCastProcessor consumes this flag to cancel active casts (WoW-authentic: movement cancels cast)
- **Telemetry:** Emits `"Position updated"` event per movement with `session_id`, old position, and new position
- **Return value:** Number of unique entities whose positions were updated (not number of events processed)
- **Test strategy:** 5 GoogleTest cases covering single entity update, unknown session skip, multiple entities, last-wins for same session, and telemetry emission

### Spell Cast Processor (`src/server/events/spellcast.h`)
- **Implementation:** `wow::SpellCastProcessor::process()` runs a 5-step phase pipeline per tick:
  1. **Movement cancellation** — if `moved_this_tick && is_casting`: cancel cast (WoW-authentic)
  2. **Interrupt events** — `INTERRUPT` events cancel targeted casts
  3. **Advance timers** — decrement `cast_ticks_remaining`; complete if 0
  4. **Process CAST_START** — GCD check, already-casting check, initiate new cast
  5. **Clear flags** — reset `moved_this_tick` on all entities
- **GCD semantics:** `kGlobalCooldownTicks = 30` (1.5s at 20 Hz). GCD triggers on cast start, not completion (matches WoW). `gcd_expires_tick == 0` means no GCD; `> current_tick` means active; `<= current_tick` means expired
- **Instant casts:** `cast_time_ticks = 0` starts and completes same tick. Sets GCD, emits both telemetry events, does not enter `is_casting` state
- **Result struct:** `SpellCastResult` with `casts_started`, `casts_completed`, `casts_interrupted`, `gcd_blocked` counters for telemetry and testing
- **Unknown sessions:** Skipped with error telemetry for CAST_START; silently skipped for INTERRUPT (entity may have been removed)
- **Telemetry:** Emits `"Cast started"` (with spell_id, session_id, cast_time_ticks), `"Cast completed"` (with spell_id, session_id), `"Cast interrupted"` (with spell_id, session_id, reason), `"Cast blocked by GCD"` (with timing details)
- **Test strategy:** 24 GoogleTest cases in 8 groups: SpellCastEvent data (3), CastState/Entity (3), cast initiation (3), GCD enforcement (3), timer advancement/completion (4), interrupt handling (3), telemetry (3), tick integration (2)

### Entity (`src/server/world/entity.h`)
- **Implementation:** `wow::Entity` represents a player's in-world avatar, keyed by `session_id`. Stores a `wow::Position` (3D float: x, y, z) and a `wow::CastState` for spell casting. Default position is origin (0, 0, 0)
- **Position struct:** `wow::Position` with `operator==`, `operator!=`, and `distance()` free function (Euclidean distance)
- **CastState struct:** Per-entity spell casting state with `is_casting`, `spell_id`, `cast_ticks_remaining`, `gcd_expires_tick`, and `moved_this_tick` flag. Accessed via mutable/const `cast_state()` accessors. Owned by game thread alongside Position — avoids circular dependency with events/
- **MVP simplification:** Entity keyed by session_id — one entity per player. NPCs with independent entity IDs deferred to Step 8 (combat)
- **Entity map:** `std::unordered_map<uint64_t, Entity>` owned by the game thread. Not mutex-protected (single-owner design)
- **Test strategy:** 3 GoogleTest cases for entity basics + 3 for CastState (default values, mutable access, moved_this_tick flag)

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

### Session State Machine (`src/server/session.h`)
- **Implementation:** `wow::Session` class with `wow::SessionState` enum (6 states) and `wow::SessionEvent` enum (7 events). Each session auto-assigns a unique monotonic ID via `std::atomic<uint64_t>` counter
- **States:** `CONNECTING → AUTHENTICATING → IN_WORLD → TRANSFERRING → DISCONNECTING → DESTROYED`
- **Pattern:** Transition table — 10-entry `constexpr std::array<SessionTransition, 10>` of `{from_state, event, to_state}` tuples. All transitions flow through one `transition()` method that performs a linear scan (O(10), appropriate for table size)
- **Valid transition:** Updates state, emits telemetry event with `session_id`, `from_state`, `to_state`, `event` fields, returns `true`
- **Invalid transition:** State unchanged, emits telemetry error with `session_id`, `current_state`, `event` fields, returns `false`
- **Telemetry guard:** `Logger::is_initialized()` check before emission — allows construction and use without requiring Logger setup (important for unit tests and early server bootstrap)
- **Ownership:** Non-copyable, movable — enables ownership transfer from accept queue to zone
- **String conversion:** `session_state_to_string()` and `session_event_to_string()` free functions returning `std::string_view`
- **Scope boundary:** Pure in-memory state machine. No networking, no zone assignment, no reconnection timer, no sweep logic. Action callbacks deferred until networking needs side effects
- **Test strategy:** 21 GoogleTest cases covering construction (3), valid transitions (10, one per table entry), invalid transitions (3), telemetry emission (3), and string conversion (2)

**Transition Table:**
```
CONNECTING     + AUTHENTICATE_SUCCESS → AUTHENTICATING
AUTHENTICATING + ENTER_WORLD          → IN_WORLD
IN_WORLD       + DISCONNECT           → DISCONNECTING
IN_WORLD       + BEGIN_TRANSFER       → TRANSFERRING
TRANSFERRING   + TRANSFER_COMPLETE    → IN_WORLD
TRANSFERRING   + DISCONNECT           → DISCONNECTING
DISCONNECTING  + RECONNECT            → AUTHENTICATING
DISCONNECTING  + TIMEOUT              → DESTROYED
CONNECTING     + DISCONNECT           → DESTROYED
AUTHENTICATING + DISCONNECT           → DISCONNECTING
```

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
