# Architecture — WoW Server Simulator

> **Status:** Living document. Updated with every component addition or modification.

## High-Level Overview

The WoW Server Simulator consists of a C++17 game server and a Python tooling suite. The server handles game simulation (tick loop, sessions, combat, spells, zones) while Python tools provide observability, fault injection, and traffic generation.

See the [System Overview Diagram](diagrams/system-overview.md) for an interactive Mermaid version of this architecture.

## Architecture Diagrams

Detailed Mermaid diagrams are available in [`docs/diagrams/`](diagrams/). GitHub renders these natively in markdown files.

| Diagram | Description |
|---------|-------------|
| [System Overview](diagrams/system-overview.md) | High-level component diagram — server threads, queues, zones, Python tools, and ports |
| [Thread Model](diagrams/thread-model.md) | 3-thread / 3-queue concurrency model with tick ordering |
| [Tick Pipeline](diagrams/tick-pipeline.md) | Per-zone 8-phase pipeline with fault injection points and exception guard |
| [Session State Machine](diagrams/session-state-machine.md) | 6-state session lifecycle with 10 transitions |
| [Python Tool Composition](diagrams/python-tools.md) | Module dependency graph — core modules, composer modules, shared models |

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
- **Event types:** `wow::MovementEvent` carries a target `Position`. `wow::SpellCastEvent` carries `SpellAction` (CAST_START/INTERRUPT), `spell_id`, and `cast_time_ticks`. `wow::CombatEvent` carries `CombatAction` (ATTACK), `target_session_id`, `base_damage`, and `DamageType` (PHYSICAL/MAGICAL)
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

### Combat Processor (`src/server/events/combat.h`)
- **Implementation:** `wow::CombatProcessor::process()` runs a 3-step pipeline per tick:
  1. **Process ATTACK events** — validate attacker/target exist and are alive, compute mitigation, apply damage, update threat table, inline death check
  2. **NPC auto-attack** — each living NPC with a non-empty threat table attacks the highest-threat living target using `base_attack_damage`
  3. **Dead entity cleanup** — remove dead entity IDs from all living entities' threat tables
- **Damage formula:** `actual = round(base_damage * (1 - clamp(mitigation, 0.0, 0.75)))`. `kMaxMitigation = 0.75f`. PHYSICAL uses `armor`, MAGICAL uses `resistance` (per ADR-012)
- **Threat model:** Damage dealt = threat generated. `target.threat_table[attacker_id] += float(actual_damage)`. Multiple attacks accumulate additively. Dead entities cleaned from all tables at end of tick
- **NPC auto-attack:** After player events resolve, each living NPC with `base_attack_damage > 0` and non-empty threat table attacks the living player with highest accumulated threat. Uses same `apply_damage` helper with PHYSICAL damage type. Creates the boss-fight loop: players attack boss, boss retaliates against tank
- **Inline death check:** After each damage application, checks `health <= 0` and marks `is_alive = false` immediately. Prevents double-damage to newly-dead entities within the same tick
- **Result struct:** `CombatResult` with `attacks_processed`, `attacks_missed`, `kills`, `npc_attacks`, `total_damage_dealt` counters for telemetry and testing
- **Telemetry:** Emits `"Damage dealt"` (with attacker_id, target_id, base_damage, actual_damage, damage_type, mitigation, target_health), `"Entity killed"` (with target_id, killer_id)
- **Test strategy:** 26 GoogleTest cases in 8 groups: CombatEvent data (3), CombatState/Entity (3), damage application (3), attack validation (4), kill/death (3), threat table (3), NPC auto-attack (3), telemetry + integration (4)

### Entity (`src/server/world/entity.h`)
- **Implementation:** `wow::Entity` represents a player or NPC's in-world avatar. Players keyed by `session_id`, NPCs by NPC ID (convention: `>= 1,000,000`). Stores `wow::Position`, `wow::CastState`, and `wow::CombatState`. Default position is origin (0, 0, 0)
- **EntityType enum:** `wow::EntityType` (`PLAYER`, `NPC`). Constructor defaults to PLAYER: `Entity(uint64_t id, EntityType type = EntityType::PLAYER)`. Accessed via `entity_type()`. NPCs participate in the same entity map as players
- **Position struct:** `wow::Position` with `operator==`, `operator!=`, and `distance()` free function (Euclidean distance)
- **CastState struct:** Per-entity spell casting state with `is_casting`, `spell_id`, `cast_ticks_remaining`, `gcd_expires_tick`, and `moved_this_tick` flag. Accessed via mutable/const `cast_state()` accessors
- **CombatState struct:** Per-entity combat state with `health` (int32_t, default 100), `max_health`, `armor` (physical mitigation, 0.0–0.75), `resistance` (magical mitigation, 0.0–0.75), `is_alive`, `base_attack_damage` (NPC auto-attack, 0 for players), and `threat_table` (`unordered_map<uint64_t, float>`). Accessed via mutable/const `combat_state()` accessors
- **Entity map:** `std::unordered_map<uint64_t, Entity>` owned per-zone (each Zone owns its entity map). Not mutex-protected (single-owner design). Zone transfers use `Zone::take_entity()` → `Zone::add_entity()` to move entities between zones while preserving state
- **Test strategy:** 3 GoogleTest cases for entity basics + 3 for CastState + 3 for CombatState/EntityType

### Zone — Self-Contained Processing Unit (`src/server/world/zone.h`)
- **Implementation:** `wow::Zone` class with `wow::ZoneConfig` (zone_id, name), `wow::ZoneState` enum (`ACTIVE`, `DEGRADED`, `CRASHED`), per-zone `wow::EventQueue`, and owned processor instances (`MovementProcessor`, `SpellCastProcessor`, `CombatProcessor`)
- **Type aliases:** `wow::ZoneId` (`uint32_t`), `wow::kNoZone` (`0`) sentinel value
- **Entity ownership:** Each Zone owns its `std::unordered_map<uint64_t, Entity>`. `add_entity()`, `remove_entity()`, `has_entity()`, `entity_count()`, `entities()` for management. `take_entity()` uses `unordered_map::extract()` + move for zero-copy zone transfers preserving position, cast, and combat state
- **Event delivery:** `push_event()` delegates to thread-safe `EventQueue`. `event_queue_depth()` for telemetry. Two-stage model per ADR-009: global intake → `ZoneManager::route_events()` → per-zone queues
- **Tick pipeline:** `tick(uint64_t current_tick)` drains queue, runs pre_tick_hook → Movement → SpellCast → Combat → post_tick_hook. Returns `ZoneTickResult` with events_processed, entities_moved, spell_result, combat_result, duration_ms, had_error, error_message
- **Exception guard:** Entire tick wrapped in `try/catch`. `std::exception` and unknown exceptions both caught, logged, and reported in `ZoneTickResult`. Zone state transitions to CRASHED on exception
- **State recovery:** CRASHED → DEGRADED → ACTIVE on successive successful ticks, visible in telemetry for monitoring dashboard
- **Fault injection hooks:** `set_pre_tick_hook()`/`set_post_tick_hook()` accept `std::function<void()>`. Map to `FaultPreTickPhase`/`FaultPostTickPhase` from ADR-009. Used by Step 10 fault injection framework
- **Health:** `health()` returns `ZoneHealth` snapshot (zone_id, state, total_ticks, error_count, entity_count, event_queue_depth, last_tick_duration_ms)
- **Telemetry:** Emits `"Zone tick completed"` metric (with zone_id, events_processed, duration_ms) on success, `"Zone tick exception"` error (with zone_id, error message) on failure. Guarded by `Logger::is_initialized()`
- **Test strategy:** 18 GoogleTest cases in 6 groups: construction (2), entity management (4), event delivery (2), tick pipeline (4), exception guard (4), telemetry (2)

### ZoneManager — Hub-and-Spoke Coordinator (`src/server/world/zone_manager.h`)
- **Implementation:** `wow::ZoneManager` class owning all `Zone` instances via `unordered_map<ZoneId, unique_ptr<Zone>>`. Maintains `session_id → zone_id` mapping for event routing and session lifecycle
- **Zone lifecycle:** `create_zone(config)` returns zone_id. `get_zone(id)` returns pointer (nullptr if not found). `zone_count()` for introspection
- **Session assignment:** `assign_session(session_id, zone_id)` creates an `Entity` in the target zone and records the mapping. Fails if zone doesn't exist or session already assigned. `remove_session(session_id)` removes entity from zone and clears mapping
- **Session transfer:** `transfer_session(session_id, target_zone_id)` extracts entity from source zone via `take_entity()`, adds to target zone. Preserves entity state (position, combat, cast). Fails gracefully if source/target zone missing
- **Event routing:** `route_events(vector<unique_ptr<GameEvent>>&)` looks up each event's `session_id()` in session-zone map, pushes to target zone's queue. Events for unassigned sessions logged and discarded (natural cross-zone isolation per ADR-008)
- **Tick orchestration:** `tick_all(current_tick)` ticks all zones sequentially (MVP). Returns `ZoneManagerTickResult` with zones_ticked, total_events, zones_with_errors, and per-zone results. Per-zone exception guard ensures crashed zone does not affect others
- **Test strategy:** 12 GoogleTest cases in 5 groups: zone lifecycle (2), session assignment (3), session transfer (2), event routing (3), tick-all and crash isolation (2)

### Tick Pipeline (Per Zone, Per Tick)
```
FaultPreTickPhase       ← pre_tick_hook (fault injection point)
DrainIntakePhase        ← event_queue_.drain() → events vector
MovementPhase           ← MovementProcessor::process() — position updates, set moved_this_tick
SpellCastPhase          ← SpellCastProcessor::process() — cancel/interrupt/advance/start/clear
CombatPhase             ← CombatProcessor::process() — damage, threat, NPC auto-attack, cleanup
FaultPostTickPhase      ← post_tick_hook (fault injection point)
StateRecoveryPhase      ← CRASHED → DEGRADED → ACTIVE on success
TelemetryEmitPhase      ← emit zone tick completed metric
```

See the [Tick Pipeline Diagram](diagrams/tick-pipeline.md) for an annotated Mermaid version with fault injection points highlighted.

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

See the [Session State Machine Diagram](diagrams/session-state-machine.md) for a visual state diagram of these transitions.

### Telemetry (`src/server/telemetry/logger.h`)
- **Implementation:** `wow::Logger` singleton, initialized via `Logger::initialize(config)`, accessed via `Logger::instance()`
- **Format:** JSON Lines with schema versioning (`"v": 1`). Each line is a self-contained JSON object with fields: `v`, `timestamp`, `type`, `component`, `message`, and optional `data`
- **Timestamp:** ISO 8601 with millisecond precision (`YYYY-MM-DDTHH:MM:SS.mmmZ`), UTC via `gmtime_r`/`gmtime_s`
- **Types:** `"metric"`, `"event"`, `"health"`, `"error"` — mapped from `wow::LogType` enum
- **Sinks:** Configurable via `LoggerConfig` — file (append mode, flushed per line for crash durability), stdout (Docker-friendly), custom `std::ostream*` (test injection). UDP broadcast deferred until networking is in place (ADR-004)
- **Thread safety:** `std::mutex` guards sink writes; `format_entry()` runs lock-free (reads immutable config, uses thread-safe time functions)
- **Convenience API:** `metric()`, `event()`, `health()`, `error()` wrappers delegate to `log()` with the appropriate `LogType`
- **Test strategy:** 28 GoogleTest cases using `std::ostringstream` as custom sink — covers schema compliance, type mapping, data handling, multi-line output, file I/O, and concurrent writes

### Control Channel (`src/control/control_channel.h`)
- **Implementation:** `wow::ControlChannel` class with `wow::ControlChannelConfig` (port, default 8081). Runs `asio::io_context` on a dedicated `std::thread`, mirroring `GameServer` pattern. Accepts newline-delimited JSON commands for runtime fault injection
- **Protocol:** Newline-delimited JSON over TCP. Five commands: `activate` (with fault_id, params, target_zone_id, duration_ticks), `deactivate` (with fault_id), `deactivate_all`, `status` (with fault_id), `list`. Responses include `success` boolean, `command` echo, and command-specific data or `error` message
- **Thread safety:** `wow::CommandQueue` (mutex + swap drain, per ADR-017) is the sole shared state between network and game threads. Network thread parses JSON and pushes `ControlCommand` structs; game thread calls `process_pending_commands()` once per tick to drain and execute against `FaultRegistry`. Response write-back via `on_complete` callback + `asio::post()` to network thread's io_context
- **Error handling:** JSON parse errors handled directly on network thread (no queue round-trip). Missing `command` field and unknown commands return `{"success": false, "error": "..."}` after queue round-trip
- **Lifecycle:** `start()` binds acceptor (port 0 = OS-assigned), begins async accept, spawns network thread. `stop()` closes acceptor, closes all clients, stops io_context, joins thread. Destructor calls `stop()` for RAII. Double-start and double-stop are no-ops via `compare_exchange_strong`
- **Client tracking:** `std::vector<weak_ptr<asio::ip::tcp::socket>>` registry with `atomic<size_t>` count. Expired weak_ptrs cleaned on disconnect
- **Helpers:** `fault_mode_to_string()` converts `FaultMode` enum to protocol strings ("tick_scoped"/"ambient"). `fault_status_to_json()` converts `FaultStatus` snapshot to JSON for status/list responses
- **Tick ordering:** `control.process_pending_commands()` → `registry.on_tick(tick)` → `zone_manager.tick_all(tick)` — fault activation takes effect on the same tick it's processed
- **Telemetry:** Emits `"Control channel started"` (with port), `"Control channel stopped"`, `"Control client connected"` (with client_count), `"Control client disconnected"` (with client_count)
- **Test strategy:** 22 GoogleTest cases in 7 groups: CommandQueue (3), construction/lifecycle (4), connection handling (3), activate command (3), deactivate commands (3), status/list commands (3), error handling (3). Tests use port 0 with `send_command` helper (poll-based read with 2s timeout)

### Session Event Queue (`src/server/session_event_queue.h`)
- **Implementation:** `wow::SessionEventQueue` — thread-safe queue bridging session lifecycle events from the network thread to the game thread. Follows the same mutex + swap-drain pattern as `EventQueue` and `CommandQueue` (ADR-002, ADR-017, ADR-022)
- **Event types:** `SessionEventType::CONNECTED` (pushed on accept) and `SessionEventType::DISCONNECTED` (pushed on client close). `SessionNotification` carries type + session_id
- **GameServer integration:** `GameServer::set_session_event_queue(SessionEventQueue*)` — non-owning raw pointer per project convention. Null-safe: no crash if no queue is set. Queue lives on the stack in `main()`
- **Game loop consumption:** Drained at tick start. CONNECTED events trigger `zone_manager.assign_session()` with round-robin zone assignment (odd session_id → zone 1, even → zone 2). DISCONNECTED events trigger `zone_manager.remove_session()`
- **Thread safety:** Single `std::mutex` protects all operations. `push()` appends to internal vector; `drain()` uses `swap()` for O(1) bulk transfer
- **Test strategy:** 4 GoogleTest cases covering empty construction, push/drain round-trip, drain-clears semantics, and concurrent multi-thread push. 3 additional tests on GameServer for connect/disconnect event notifications and null-queue safety

### Fault Injection (`src/server/fault/`)
- **Architecture:** `wow::FaultRegistry` owns all registered `wow::Fault` instances via `unordered_map<FaultId, unique_ptr<Fault>>`. Not a singleton — created and owned by the game server for testability
- **Base class:** `wow::Fault` abstract base with `id()`, `mode()`, `activate()`, `deactivate()`, `is_active()`, `on_tick(tick, zone*)`, `status()`. `FaultMode` enum distinguishes `TICK_SCOPED` (fires per-zone via pre_tick_hook) from `AMBIENT` (runs globally when activated)
- **Configuration:** `wow::FaultConfig` carries fault-specific `nlohmann::json params`, `target_zone_id` (0 = all zones), and `duration_ticks` (0 = indefinite). `FaultStatus` snapshot for telemetry and monitoring
- **Zone hook wiring:** Game loop sets each zone's pre_tick_hook to call `registry.execute_pre_tick_faults(zone)`, which runs inside the zone's exception guard. The registry's `on_tick()` is called before `zone_manager.tick_all()` to drive ambient faults and duration tracking
- **Zone targeting:** `execute_pre_tick_faults()` checks `target_zone_id` — 0 matches all zones, specific ID matches only that zone. Ambient faults are excluded from pre-tick execution
- **Duration tracking:** Registry stores per-fault `ActivationInfo` with config and `ticks_elapsed` counter. `on_tick()` increments elapsed and auto-deactivates when `duration_ticks > 0 && ticks_elapsed >= duration_ticks`
- **Telemetry:** Emits `"Fault activated"` and `"Fault deactivated"` events via Logger on activate/deactivate
- **F1 LatencySpikeFault** (`TICK_SCOPED`): `sleep_for(delay_ms)` in `on_tick()`. Default 200ms, configurable via `params["delay_ms"]`. Simulates processing latency visible as tick rate degradation
- **F2 SessionCrashFault** (`TICK_SCOPED`): Removes first entity from zone via `zone->remove_entity()`. Fire-once flag per activation — re-activation resets. Safe on empty zones. Emits telemetry with victim session_id
- **F3 EventQueueFloodFault** (`TICK_SCOPED`): Pushes `multiplier * entity_count` synthetic `MovementEvent`s into zone queue via `zone->push_event()`. Default multiplier 10, configurable via `params["multiplier"]`. Deterministic positions via hash formula (no `<random>` header). Events pushed in pre-tick hook, drained and processed in same tick
- **F4 MemoryPressureFault** (`AMBIENT`): Allocates `megabytes` MB in 1MB `vector<uint8_t>` chunks filled with 0xAB (ensures OS page commit). Releases on deactivation. `bytes_allocated()` accessor. Default 64 MB, configurable via `params["megabytes"]`
- **F5 CascadingZoneFailureFault** (`TICK_SCOPED`): Multi-phase fault. Phase 1: throws `std::runtime_error` in source zone (zone exception guard catches → zone goes CRASHED). Phase 2: floods target zone with `flood_multiplier * entity_count` synthetic MovementEvents (reuses F3 pattern). Configurable via `params["source_zone"]` (default 1), `params["target_zone"]` (default 2), `params["flood_multiplier"]` (default 10). Demonstrates cascading failure where one zone crash overloads another
- **F6 SlowLeakFault** (`TICK_SCOPED`): Increments tick processing delay over time. Every `increment_every` ticks (default 100), adds `increment_ms` (default 1) to accumulated sleep delay. Simulates gradual performance degradation (memory leak, cache bloat). `current_delay_ms()` accessor for status reporting. Detection signal: tick duration_ms in telemetry grows linearly
- **F7 SplitBrainFault** (`TICK_SCOPED`): Creates `phantom_count` (default 2) NPC entities with IDs starting at `phantom_base_id` (default 2000001) in each zone. Injects divergent MovementEvents per tick: odd zone_id moves east (x-axis), even zone_id moves north (y-axis). Same entity ID at different positions in different zones = state divergence detection signal
- **F8 ThunderingHerdFault** (`TICK_SCOPED`): Phase 1: removes all PLAYER entities from each zone (NPCs preserved), stores their IDs. Phase 2: after `reconnect_delay_ticks` (default 20 = 1s at 20Hz), re-adds all stored players as fresh Entity objects simultaneously. Burst of reconnections creates thundering herd pattern in telemetry. Bypasses ZoneManager's session_zone_map intentionally — events for removed entities produce "unknown session" errors (detection signal)
- **Test strategy:** 42 GoogleTest cases in 12 groups: registry registration (3), activation/deactivation (4), duration/status/telemetry (4), F1 latency spike (3), F2 session crash (4), F3 event flood (3), F4 memory pressure (3), zone integration (4), F5 cascading zone failure (4), F6 slow leak (3), F7 split brain (3), F8 thundering herd (4)

### Python Tooling (`wowsim` CLI)
- **Package:** Installable via `pip install -e tools/`, provides `wowsim` command
- **Shared models:** `wowsim.models` — Pydantic v2 models for telemetry entries, summaries, anomalies, and control channel protocol (ADR-018, ADR-019)
- **Async strategy:** Async core, sync CLI surface via `asyncio.run()`

### Log Parser (`wowsim/log_parser.py`)
- **Responsibility:** Parse JSONL telemetry files, filter/summarize entries, detect anomalies
- **Parsing:** `parse_line()` validates individual JSON lines into `TelemetryEntry` via Pydantic. `parse_file()` and `parse_stream()` handle bulk input, silently skipping malformed lines
- **Filtering:** `filter_entries()` supports composable type, component, and message substring filters
- **Summary:** `summarize()` computes entry counts by type/component, error count, time range, and duration
- **Anomaly detection:** `detect_anomalies()` runs four detectors with configurable thresholds:
  - `_detect_latency_spikes` — game_loop tick metrics with `duration_ms` exceeding warn (60ms) or critical (100ms) thresholds
  - `_detect_zone_crashes` — zone error entries with "Zone tick exception" message
  - `_detect_error_bursts` — sliding window counting errors within configurable time window (default: 5+ in 10s)
  - `_detect_unexpected_disconnects` — game_server "Client disconnected" events
- **CLI:** `wowsim parse-logs <FILE>` with `--type`, `--component`, `--message`, `--anomalies`, and `--format json|text` options
- **Output:** Human-readable summary/anomaly tables (default), or structured JSON via `ParseResult.model_dump_json()`
- **Test strategy:** 20 pytest cases in 6 groups: model parsing (3), file/stream parsing (3), filtering (4), summary (3), anomaly detection (5), CLI integration (2)

### Fault Trigger Client (`wowsim/fault_trigger.py`)
- **Responsibility:** TCP client for the C++ control channel, enabling operators to activate/deactivate faults at runtime
- **Async core:** `ControlClient` uses `asyncio.open_connection` for non-blocking TCP I/O. Context manager (`async with`) for connection lifecycle. Five command methods: `activate`, `deactivate`, `deactivate_all`, `status`, `list_faults`
- **Protocol:** Newline-delimited JSON over TCP. Request models (`FaultActivateRequest`, etc.) serialize via `model_dump()`. Responses parsed via `ControlResponse.model_validate_json()`
- **Sync wrappers:** `activate_fault`, `deactivate_fault`, `deactivate_all_faults`, `fault_status`, `list_all_faults` — each opens a fresh connection via `asyncio.run()` for CLI one-shot use
- **Duration parsing:** `parse_duration("5s")` → 100 ticks (5 × 20 Hz), `parse_duration("100t")` → 100 ticks. Abstracts tick rate from operators
- **Error handling:** `ControlClientError` raised on `success=false` server responses. `OSError` propagated on connection failures
- **Formatting:** `format_fault_info()` (one-line summary per fault), `format_fault_list()` (table with header)
- **CLI:** `wowsim inject-fault` group with `--host`/`--port`, subcommands: `activate FAULT_ID [--delay-ms, --megabytes, --multiplier, --duration, --zone]`, `deactivate FAULT_ID`, `deactivate-all`, `status FAULT_ID`, `list`
- **Reuse:** Async `ControlClient` designed for direct use by the Textual dashboard (Step 17) without blocking the event loop
- **Test strategy:** 20 pytest cases in 5 groups: models (3+2), duration parsing (2+3), client commands (4), error handling (3), CLI integration (3). Mock TCP server fixture in conftest.py (threading-based, ephemeral port, canned responses)

### Health Check Reporter (`wowsim/health_check.py`)
- **Responsibility:** Aggregate server health from telemetry logs and present periodic health summaries
- **Data source:** Reads JSONL telemetry log file (tail-based, last 500 lines by default). No C++ changes needed — all health data is already emitted by the server
- **Pure computation core:** Four stateless functions operating on `list[TelemetryEntry]`:
  - `compute_tick_health()` — extracts tick rate stats (avg/max/min duration, overrun count/percentage) from `game_loop` "Tick completed" metrics
  - `compute_zone_health()` — groups zone tick metrics and errors by zone_id, determines state (ACTIVE/CRASHED)
  - `estimate_player_count()` — net "Connection accepted" minus "Client disconnected" events
  - `determine_status()` — three-tier evaluation: critical (critical anomalies or CRASHED zones), degraded (warnings, DEGRADED zones, >10% overruns), healthy (otherwise)
- **Module reuse:** `log_parser.parse_line()` for entry parsing, `log_parser.detect_anomalies()` for anomaly detection, `fault_trigger.list_all_faults()` for active fault status (ADR-020)
- **I/O helpers:** `check_server_reachable()` TCP connect probe, `read_recent_entries()` tail-based log reader
- **Orchestration:** `build_health_report()` composes all data sources into a `HealthReport` model. Designed for reuse by the dashboard (Step 17)
- **Formatting:** `format_health_report()` produces human-readable multi-line output with status, tick rate, zones, players, faults, and anomalies
- **Models:** `TickHealth`, `ZoneHealthSummary`, `HealthReport` (Pydantic v2) in `wowsim.models`. `HealthReport` composes existing `Anomaly` and `FaultInfo` models
- **CLI:** `wowsim health --log-file <path>` with `--watch`/`--interval` for continuous monitoring, `--format json|text`, `--no-faults` to skip control channel, `--host`/`--port`/`--control-port` for server addresses
- **Test strategy:** 20 pytest cases in 7 groups: health models (3), tick health computation (3), zone health and player count (3), status determination (3+2), server reachability (2), report building and formatting (2), CLI integration (2)

### Mock Client Spawner (`wowsim/mock_client.py`)
- **Responsibility:** Spawn N concurrent simulated players that generate WoW-realistic game traffic for stress testing and load generation
- **Traffic generation:** Pure functions producing payloads matching C++ event types exactly — `generate_movement_action` (position with ±5/±0.5 delta), `generate_spell_cast_action` (spell_id from set, cast_time from set), `generate_combat_action` (ATTACK with 10–50 damage, PHYSICAL/MAGICAL). `choose_action` selects via weighted distribution: 50% movement, 30% spell_cast, 20% combat (matching realistic player behavior patterns)
- **Async client:** `MockGameClient` uses `asyncio.open_connection` for non-blocking TCP I/O (mirroring `ControlClient` pattern from fault_trigger.py). Context manager for lifecycle. Position tracking per client provides spatial coherence across movement events
- **Orchestration:** `spawn_clients(config, count)` runs N clients concurrently via `asyncio.gather`. Each client captures its own failures independently — connection errors don't abort siblings. `run_spawn()` sync wrapper via `asyncio.run()` for CLI use
- **Per-client failure isolation:** `OSError` on connect/send produces a `ClientResult` with `connected=False` and error string. `SpawnResult` aggregates successful/failed counts and total actions
- **Models:** `ClientConfig` (host, port, rate, duration), `ClientResult` (per-client outcome), `SpawnResult` (aggregate with client list) — Pydantic v2 in `wowsim.models`
- **Formatting:** `format_spawn_result()` produces human-readable table with summary header (total/connected/failed/actions/duration) and per-client status lines
- **CLI:** `wowsim spawn-clients --count 10 --duration 60 --host localhost --port 8080 --rate 2 --format text|json`
- **Test strategy:** 18 pytest cases in 7 groups: models (3), traffic generation (3), action selection (2), single client lifecycle (3), multi-client spawning (3), formatting (2), CLI integration (2). `mock_game_server` fixture (ThreadingTCPServer on ephemeral port) in conftest.py

### Monitoring Dashboard (`wowsim/dashboard.py`)
- **Responsibility:** Live TUI dashboard displaying server health metrics, zone status, fault control, and scrolling event log with auto-refresh
- **Framework:** Textual 8.0 TUI framework with CSS-based layout (`dashboard.tcss`)
- **Widget tree:** `WoWDashboardApp(App)` → Header, StatusBar (Static), Horizontal(TickMetricsPanel (Static) + ZoneTable (DataTable)), FaultControlPanel (Static), EventLog (RichLog), Footer
- **Data flow — dual strategy (ADR-023):**
  - Health refresh: `@work(thread=True)` runs sync functions from `health_check.py` and `log_parser.py` in a worker thread. `call_from_thread()` marshals results back for UI updates
  - Fault queries: `@work(exclusive=True)` runs async `ControlClient` directly on the Textual event loop
- **Timestamp watermark:** `filter_new_entries()` tracks last-seen timestamp to append only new entries to the RichLog, preventing duplicates
- **Pure formatting functions:** `format_status_bar`, `format_tick_panel`, `format_event_line`, `status_to_style`, `fault_action_label` — independently testable without Textual
- **Key bindings:** `q` (quit), `r` (manual refresh), `a` (activate first inactive fault), `d` (deactivate first active fault), `x` (deactivate all)
- **Configuration:** `DashboardConfig` (Pydantic v2) with log_file, host, port, control_port, refresh_interval
- **CLI:** `wowsim dashboard --log-file <path> --host <host> --port <port> --control-port <port> --refresh <seconds>`
- **Test strategy:** 16 pytest cases in 8 groups: status bar formatting (2), tick panel formatting (2), style mapping (1), event line formatting (2), fault label (2), entry filtering with watermark (3), config defaults/overrides (2), CLI integration (2)

### Hotfix Pipeline (`wowsim/pipeline.py`)
- **Responsibility:** Orchestrate a staged deployment lifecycle (build → validate → canary → promote/rollback) by composing existing health_check and fault_trigger modules
- **Deployment metaphor (ADR-024):** Activating a fault = deploying a change. Deactivating = deploying a fix. Rollback = reversing the action. Maps naturally to the existing fault injection system
- **Five stages:** BUILD (health gate: reachable + not critical) → VALIDATE (full health snapshot, must be reachable) → CANARY (execute deploy action, poll health at configurable intervals) → PROMOTE (canary passed, declare success) or ROLLBACK (canary failed, reverse deploy action)
- **Pure function core:** Six testable functions with no I/O:
  - `check_build_preconditions(reachable, status)` — pass if reachable and not critical (degraded allowed)
  - `check_validate_gate(report)` — pass if server reachable
  - `evaluate_canary_health(samples, threshold)` — fail on first sample at or above threshold severity
  - `determine_rollback_action(config)` — reverse activate↔deactivate
  - `format_stage_result(result)` — one-line stage summary
  - `format_pipeline_result(result)` — multi-line pipeline report
- **I/O wrappers:** Three thin mockable functions composing existing modules:
  - `_get_health_report(config)` → `health_check.build_health_report()`
  - `_execute_deploy_action(config)` → `fault_trigger.activate_fault()` or `deactivate_fault()`
  - `_execute_rollback(config)` → reverse of deploy action
- **Orchestrator:** `run_pipeline(config)` — sync function calling I/O wrappers, feeding results to pure gate functions, recording stage results. Canary uses `time.sleep()` polling
- **Models:** `PipelineConfig` (fault_id, action, canary settings, connection settings), `StageResult` (per-stage outcome), `PipelineResult` (all stages + final outcome) — Pydantic v2 in `wowsim.models`
- **CLI:** `wowsim deploy --fault-id <id> --action activate|deactivate --canary-duration <s> --canary-interval <s> --rollback-on critical|degraded --format text|json` with fault params (--delay-ms, --megabytes, --multiplier, --duration, --zone)
- **Test strategy:** 20 pytest cases in 8 groups: models (3), build preconditions (3), validate gate (2), canary evaluation (3), rollback action (2), formatting (2), orchestration with monkeypatch (3), CLI integration (2)

### Performance Benchmarks (`wowsim/benchmark.py`)
- **Responsibility:** Automated scaling tests that verify server tick stability under increasing client load, producing pass/fail reports with percentile metrics (ADR-026)
- **Composition pattern:** Composes existing tools with zero new I/O code — `mock_client.run_spawn()` for traffic generation, `health_check.compute_tick_health()` for tick analysis, `health_check.read_recent_entries()` for telemetry reading
- **Pure function core:** Six testable functions with no I/O:
  - `compute_percentiles(entries)` — P50/P95/P99 via nearest-rank method, stddev for jitter. Filters for `game_loop` "Tick completed" metrics
  - `compute_throughput(spawn_result)` — total_actions / total_duration, zero-guarded
  - `evaluate_scenario(tick_health, percentiles, throughput, count, config)` — pass when avg ≤ max_avg, p99 ≤ max_p99, overrun_pct ≤ max_overrun
  - `evaluate_benchmark(scenarios)` — all-pass logic, summary identifying max passing client count or first failing count
  - `format_scenario_result(result)` — one-line: `[PASS] 50 clients — avg 3.5ms, p99 8.2ms, 0.0% overrun`
  - `format_benchmark_result(result)` — multi-line report with header, per-scenario lines, outcome
- **I/O wrappers:** Three thin mockable functions:
  - `_spawn_clients(config, count)` → `mock_client.run_spawn()`
  - `_read_telemetry(config)` → `health_check.read_recent_entries()` (max_lines=2000)
  - `_settle(seconds)` → `time.sleep()` (monkeypatchable for fast tests)
- **Orchestrator:** `run_benchmark(config)` iterates client_counts, spawning clients (skipped for count=0 baseline), settling, reading telemetry, computing tick health + percentiles, and evaluating against thresholds
- **Default thresholds:** max avg tick 50ms (full 20Hz tick budget), max P99 tick 100ms (log_parser CRIT threshold), max overrun 5% (stricter than health_check's 10%)
- **Models:** `PercentileStats` (p50/p95/p99/jitter), `ScenarioResult` (per-count outcome), `BenchmarkConfig` (thresholds + connection settings), `BenchmarkResult` (all scenarios + overall pass/fail) — Pydantic v2 in `wowsim.models`
- **CLI:** `wowsim benchmark --log-file <path> --counts 0,10,25,50,100 --duration 10 --rate 2 --settle 2 --max-avg-tick 50 --max-p99-tick 100 --max-overrun-pct 5 --format text|json`. Exit code 0 on pass, 1 on fail for CI integration
- **Test strategy:** 20 pytest cases in 8 groups: models (3), percentile computation (3), throughput (2), scenario evaluation (3), overall evaluation (2), formatting (2), orchestration with monkeypatch (3), CLI integration (2)

## Concurrency Model

**MVP (Three Threads):**
- Game server network thread: Asio io_context, handles game client TCP I/O
- Control channel network thread: Separate Asio io_context, handles operator TCP I/O
- Game loop thread (main thread): Fixed-rate tick at 20 Hz, processes all zones sequentially
- Communication: Three thread-safe queues, all using mutex + swap drain:
  - `EventQueue` — game events from network to game thread (ADR-009)
  - `CommandQueue` — control commands from control channel to game thread (ADR-017)
  - `SessionEventQueue` — session connect/disconnect from network to game thread (ADR-022)
- Ownership boundary: Game thread owns all game state (FaultRegistry, ZoneManager, zone entities). Network threads only enqueue.
- Tick ordering: drain session events → process control commands → tick ambient faults → tick all zones

**Polish (Thread-per-Zone):**
- Each zone gets its own thread and tick loop
- ZoneManager becomes a supervisor
- Per-zone queues replace single intake queue
- No fundamental architectural change — queue interface stays the same

## Integration Test Architecture

### Test Strategy
Integration tests verify that all Python tools compose correctly in the documented workflow: **connect → play → inject fault → detect → recover**. Tests use mock servers from `conftest.py` plus synthesized JSONL telemetry log files. With `main.cpp` fully wired, tests can also be extended with a real server subprocess fixture.

### Test Infrastructure (`tests/python/integration/conftest.py`)
- **Telemetry line helpers:** Module-level functions (`make_tick_line`, `make_connection_line`, `make_disconnect_line`, `make_zone_tick_line`, `make_zone_error_line`, `make_combat_error_line`) build JSONL lines matching the C++ server's telemetry schema
- **Scenario fixtures:** 6 pytest fixtures generating deterministic JSONL log files covering normal operation, latency spikes (F1), session crashes (F2), zone crashes, recovery arcs (healthy → fault → recovery), and 50-player load
- **Timestamps:** Sequential from `2026-02-24T12:00:00.000Z` with 50ms tick increments, matching the 20 Hz server tick rate
- **Fixture reuse:** Integration conftest reuses `mock_game_server` and `mock_control_server` from the parent `tests/python/conftest.py`

### Test Organization (16 tests, 3 files)

| File | Tests | Scope |
|------|-------|-------|
| `test_connection_lifecycle.py` | 6 | Client connections + telemetry parsing + player counting |
| `test_fault_and_recovery.py` | 6 | Fault inject/deactivate + anomaly detection + health status transitions |
| `test_end_to_end.py` | 4 | Full pipeline composition + 50-client stress + all fault types |

### Tool Composition Verified
Each integration test exercises 2+ tools together:
- **mock_client** `run_spawn` → **log_parser** `parse_file` → **health_check** `estimate_player_count`
- **fault_trigger** `activate_fault` → **log_parser** `detect_anomalies` → verify anomaly type
- **health_check** `build_health_report` → **log_parser** `detect_anomalies` + **health_check** `determine_status`
- Full 5-tool pipeline: spawn → activate → detect → deactivate → verify recovery

### Extensibility
With `main.cpp` fully wired, tests can be extended with a real server subprocess fixture. The telemetry log fixtures provide a stable baseline for comparison against live server output.

## Demo Script (`scripts/demo.sh`)

A narrated, color-coded bash script that demonstrates the full server reliability lifecycle in ~70 seconds. Composes all Python CLI tools against the live C++ server.

### Phase Walkthrough

| Phase | Duration | CLI Commands | What It Shows |
|-------|----------|-------------|---------------|
| **1. Baseline** | ~15s | `spawn-clients --count 5 --duration 5`, `health --no-faults` | Healthy server with player traffic |
| **2. Break It** | ~10s | `inject-fault activate latency-spike --delay-ms 200`, `health --no-faults` | Tick degradation under 200ms fault (4x overrun) |
| **3. Diagnose** | ~3s | `parse-logs --anomalies`, `inject-fault status latency-spike` | Anomaly detection + root cause identification |
| **4. Fix It** | ~10s | `inject-fault deactivate latency-spike`, `health --no-faults` | Manual remediation and recovery verification |
| **5. Pipeline** | ~20s | `deploy --fault-id latency-spike --action activate --delay-ms 200 --canary-duration 10 --canary-interval 2 --rollback-on critical` | Automated canary deployment with rollback |
| **6. Summary** | ~5s | `health --no-faults` | Final health check and lifecycle recap |

### Cross-Platform Support
- **Binary detection:** checks `build/Debug/wow-server-sim.exe` (Windows/MSVC), `build/wow-server-sim.exe` (Windows/other), `build/wow-server-sim` (Linux)
- **Venv activation:** checks `.venv/Scripts/activate` (Windows) then `.venv/bin/activate` (Linux)
- **Colors:** ANSI escapes disabled when stdout is not a TTY

### Server Lifecycle
- Removes stale `telemetry.jsonl`, starts server in background, waits for telemetry file as readiness signal (up to 10s)
- `trap cleanup EXIT INT TERM` kills server PID on any exit path
- Server stdout redirected to `/dev/null` for clean demo output

## Key Design Patterns
- **Producer/Consumer:** Network → intake queue → game loop
- **Two-Stage Queue:** Intake → per-zone distribution
- **Command Queue:** Control channel → command queue → game thread (ADR-017)
- **Session Event Queue:** Network thread → session event queue → game thread (ADR-022)
- **Transition Table:** Session state machine
- **Phase Pipeline:** Ordered tick processing
- **Mark-and-Sweep:** Session cleanup with reconnection window
- **Exception Guard:** Zone-level fault isolation
- **Self-Registration:** Fault registry pattern
- **Worker Thread + Watermark:** Dashboard sync I/O in worker thread, timestamp watermark for incremental event log updates (ADR-023)
- **Staged Pipeline with Health Gates:** Deployment pipeline with pure gate functions, thin I/O wrappers, and monkeypatch-testable orchestrator (ADR-024)
- **Tool Composition with Percentile Evaluation:** Benchmark suite composes mock_client + health_check + percentile computation into automated scaling tests with configurable thresholds (ADR-026)
