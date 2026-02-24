# Architecture Decision Records

> Append-only log. Each decision is recorded when made and never modified (only superseded).

## ADR-001: Networking Library — Standalone Asio

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need a networking library for the TCP game server and control channel. Options: raw POSIX sockets, Boost.Asio, standalone Asio.

**Decision:** Use standalone Asio (non-Boost), header-only, integrated via CMake FetchContent.

**Consequences:**
- Battle-tested async I/O without Boost dependency weight
- Header-only, integrates in ~3 CMake lines
- Basis for C++ Networking TS — industry-standard choice
- Abstracts raw socket details (epoll, non-blocking I/O, buffer management) — documented in ARCHITECTURE.md to demonstrate understanding
- Rich documentation produces correct agentic code generation

---

## ADR-002: Game Loop Architecture — Two Threads (MVP)

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need to separate network I/O from game logic processing. Options: single-threaded, two threads, thread-per-zone, fully async on Asio io_context.

**Decision:** Two threads for MVP (network thread + game loop thread), migrating to thread-per-zone in polish phase.

**Consequences:**
- Clean producer/consumer separation via thread-safe event queue
- Teaches core concurrency primitives in a simple context
- B→C migration path is incremental — queue interface stays the same
- Shared state concerns mitigated by clear ownership boundaries (game thread owns all game state)

---

## ADR-003: Telemetry Format — JSON Lines with Schema Versioning

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need structured logging format for server telemetry consumed by Python tools.

**Decision:** JSON Lines with `"v": 1` schema version field and `"type"` field for consumer filtering (`"metric"`, `"event"`, `"health"`, `"error"`).

**Consequences:**
- Industry standard (ELK, Datadog, Splunk compatible)
- Human-readable, grep-friendly, trivial to parse in Python
- nlohmann/json makes C++ emission simple
- Schema versioning is trivial but signals production maturity

---

## ADR-004: Telemetry Transport — Log File + UDP Broadcast

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need to deliver telemetry to multiple Python consumers (dashboard, log parser, health check).

**Decision:** Log file as durable source of truth, UDP broadcast as real-time feed.

**Consequences:**
- Mirrors production telemetry pipeline pattern (Server → Buffer → MQ → Consumers)
- Multiple consumers can attach independently
- If UDP drops, file has everything — real-time view is best-effort
- Dashboard can backfill from file on startup, then switch to UDP for live updates

---

## ADR-005: Fault Injection — Control Channel with TCP/JSON

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need a way to inject faults at runtime without disrupting the game loop.

**Decision:** Separate TCP control channel using newline-delimited JSON messages. FaultRegistry with self-registration, hybrid execution model (tick-scoped interceptors + ambient faults).

**Consequences:**
- Consistent JSON format across entire system
- Self-describing messages, easy to extend
- Tick-scoped faults compose as interceptor chain
- Ambient faults compose trivially (independent)
- Designed for concurrent active faults from day one

---

## ADR-006: Session State Machine — Transition Table

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need to manage player session lifecycle with observable state transitions.

**Decision:** Central transition table of `{from_state, event, to_state, action}` tuples. States: CONNECTING → AUTHENTICATING → IN_WORLD → TRANSFERRING → DISCONNECTING → DESTROYED.

**Consequences:**
- Single source of truth for all legal transitions
- Free telemetry logging through one `transition()` function
- Invalid transitions rejected and logged — instant bug detector
- TRANSFERRING state reserved for polish phase (zone transfers)

---

## ADR-007: Session Cleanup — Mark-and-Sweep with Reconnection Window

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need safe session cleanup that handles disconnects, fault injection, and reconnection.

**Decision:** On disconnect, session transitions to DISCONNECTING with 30-second reconnection window (matching WoW). Sweep happens at end-of-tick sync point only.

**Consequences:**
- No mid-tick destruction — eliminates use-after-free and dangling references
- Reconnection window produces distinct telemetry patterns for clean vs. fault-injected disconnects
- Deterministic cleanup timing simplifies reasoning about state

---

## ADR-008: Zone Architecture — Self-Contained Processing Units

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need failure isolation between game zones and a path to thread-per-zone scaling.

**Decision:** Each zone owns its session registry, entity list, event queue, and tick pipeline. Exception guard + health monitoring for isolation. ZoneManager as coordinator (hub and spoke).

**Consequences:**
- Testable in isolation without global state
- Maps directly to thread-per-zone (today sequential, tomorrow parallel)
- Exception guard prevents fault propagation between zones
- Health monitoring detects gradual degradation before crashes

---

## ADR-009: Event Processing — Class Hierarchy with Phase Pipeline

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need to process game events (movement, spells, combat) in a deterministic order with fault injection hooks.

**Decision:** Base `GameEvent` with derived types. External processor classes per event type. Two-stage queue (intake → per-zone). Ordered phase pipeline per tick.

**Consequences:**
- Each processor independently testable
- Two-stage queue provides three natural telemetry emission points
- Phase ordering is deterministic — movement before spellcasts before combat (authentic WoW behavior)
- Fault hooks unified with pipeline (FaultPreTickPhase / FaultPostTickPhase)

---

## ADR-010: Python Tooling — Click CLI with Installable Package

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need a professional CLI surface for Python reliability tools.

**Decision:** `pyproject.toml` + `pip install -e .` providing `wowsim <command>`. Async core with sync CLI surface. Shared modules for protocol, telemetry, models, config.

**Consequences:**
- Professional CLI ergonomics (`wowsim dashboard`, `wowsim inject-fault`, etc.)
- Importable as library for tests
- Async where concurrency matters (50 mock clients), sync where simpler (CLI parsing)

---

## ADR-011: Dashboard — Textual TUI

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need a monitoring dashboard for the demo. Options: raw curses, Textual, Flask + WebSocket.

**Decision:** Textual (modern terminal UI framework).

**Consequences:**
- Near-web-UI visual quality in the terminal
- Runs in Docker, over SSH, in tmux — fits ops-engineer aesthetic
- Proper widgets: live tables, styled panels, sparklines, colored status
- No browser dependency
- Screenshot-worthy output for README

---

## ADR-012: WoW Game Mechanics — High Fidelity Simulation

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need to model WoW mechanics that demonstrate domain knowledge and create interesting fault scenarios.

**Decision:** Implement: spell casts (cast times, GCD, interrupts, movement cancellation), combat (physical/magical split with armor/resistance), threat table (modifiers, taunt, retargeting). Exclude: spell queuing.

**Consequences:**
- Every mechanic either demonstrates domain knowledge or creates an interesting fault scenario
- Cross-phase interactions (movement cancels cast) demonstrate pipeline design
- Threat retargeting on disconnect creates visible fault cascades
- Constants match real WoW values (20 Hz tick, 1.5s GCD, etc.)

---

## ADR-013: Connection Wrapper Separate from Session

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need to bridge TCP sockets (network layer) with Sessions (game layer). Options: embed socket in Session, use a separate Connection wrapper, or use a flat struct.

**Decision:** Separate `Connection` class that owns a `Session` by value and a TCP socket. `Connection` uses `enable_shared_from_this` for safe async callback capture. `GameServer` stores `shared_ptr<Connection>` in a registry keyed by session ID.

**Consequences:**
- Session remains a pure state machine with no networking dependencies — testable in isolation
- Connection handles async I/O lifetime (shared_from_this prevents destruction during pending operations)
- Clean separation of concerns: Session knows state transitions, Connection knows sockets
- Connection's disconnect callback lets GameServer remove entries from the registry without coupling
- Session is non-copyable/movable, Connection is non-copyable (shared_ptr ownership) — clear ownership semantics

---

## ADR-014: Event Ownership — unique_ptr with Session-ID Keying

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need an ownership model for game events flowing from network thread through event queue to game thread processors. Options: shared_ptr (multi-owner), unique_ptr (single-owner), value semantics (copy), raw pointers with arena.

**Decision:** `std::unique_ptr<GameEvent>` for single-ownership transfer. Events carry a `session_id` (not a pointer to Session/Entity) for loose coupling. Entity map keyed by `session_id` (`std::unordered_map<uint64_t, Entity>`). Type tag enum (`EventType`) enables safe `static_cast` downcasting without RTTI. Position struct defined in `entity.h` (world concept), included by `movement.h`.

**Consequences:**
- Zero-copy transfer through EventQueue (move semantics only)
- No dangling references — events don't hold pointers to sessions or entities
- Session-ID lookup at processing time handles disconnects gracefully (unknown session = skip + warn)
- Type tag pattern avoids RTTI overhead of dynamic_cast — standard game engine technique
- Entity map not mutex-protected — owned exclusively by game thread, consistent with ADR-002 ownership boundaries
- Position in entity.h avoids circular dependency (entity.h ← movement.h, not bidirectional)

---

## ADR-015: Spell Cast System — CastState on Entity with Phase-Ordered Processing

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need spell casting mechanics (cast times, GCD, interrupts, movement cancellation) that demonstrate WoW domain knowledge and create interesting fault injection scenarios. Key design decisions: where to store cast state, how to communicate between MovementProcessor and SpellCastProcessor, and processing order within the spell cast phase.

**Decision:**
1. **CastState struct in entity.h** alongside Position — both are per-entity state owned by the game thread. Avoids circular dependency (entity.h doesn't depend on events/).
2. **Movement-cancels-cast via flag:** `MovementProcessor` sets `moved_this_tick = true`; `SpellCastProcessor` checks and clears it. Keeps processor APIs uniform.
3. **SpellAction enum (CAST_START/INTERRUPT)** on SpellCastEvent — cast time carried directly, no spell registry for MVP.
4. **5-step processing order:** movement cancellation → interrupt events → advance timers → CAST_START → clear flags. Movement cancels before events ensures you can't cast and move same tick. Interrupts before advancement prevents complete-and-interrupt in same tick. CAST_START last ensures GCD from completed casts is respected.
5. **GCD on cast start:** `gcd_expires_tick = current_tick + 30` set when cast initiates, not when it completes (matches WoW). Instant casts (cast_time=0) set GCD and complete same tick.

**Consequences:**
- Cross-phase communication via flag on entity is simple and testable — no extra parameters or return values
- Processing order matches WoW's actual behavior and prevents same-tick exploits
- GCD semantics use absolute tick numbers, avoiding timer drift
- SpellCastResult struct enables rich telemetry without requiring log parsing in tests
- Instant casts are a natural special case, not a separate code path

---

## ADR-016: Combat System — Damage, Threat, and NPC Auto-Attack

**Date:** 2026-02-23
**Status:** Accepted

**Context:** Need combat mechanics (damage calculation, threat tracking, NPC targeting) that demonstrate WoW domain knowledge and create meaningful fault injection scenarios. Key design decisions: where to store combat state, damage formula, threat model, NPC auto-attack strategy, and death handling within a single tick.

**Decision:**
1. **CombatState struct in entity.h** alongside Position and CastState — all per-entity state owned by the game thread. Uses `int32_t` health (not unsigned) to avoid underflow on damage, simplifying death check (`health <= 0`). Includes `threat_table` (`unordered_map<uint64_t, float>`) — each entity tracks who has threat on it (WoW model: each mob has its own threat list).
2. **EntityType enum (PLAYER/NPC)** on Entity with default PLAYER — existing code unaffected. NPC entities added to the same entity map using NPC IDs (convention: `>= 1,000,000`).
3. **DamageType enum (PHYSICAL/MAGICAL)** selects mitigation: PHYSICAL → armor, MAGICAL → resistance. Formula: `actual = round(base * (1 - clamp(mitigation, 0, 0.75)))`. `kMaxMitigation = 0.75f` prevents full immunity.
4. **Threat = damage dealt (no modifiers)** per ADR-012. Accumulates additively across attacks. Dead entities cleaned from all threat tables at end of tick.
5. **3-step processing order:** process ATTACK events → NPC auto-attack → clean up dead from threat tables. Inline death check after each damage application prevents double-damage to newly-dead entities within the same tick.
6. **NPC auto-attack:** After player events resolve, each living NPC with `base_attack_damage > 0` attacks the highest-threat living target. Uses same damage pipeline (mitigation, death check, telemetry). Creates the classic boss-fight loop: players attack boss, boss retaliates against tank.

**Consequences:**
- CombatState on Entity follows the same pattern as CastState — consistent, testable, no circular dependencies
- EntityType enables NPCs without architectural change — same entity map, same processors
- Inline death checking prevents exploit where multiple attacks kill an already-dead entity in the same tick
- NPC auto-attack using threat table creates emergent gameplay (tank/DPS/threat management) that produces interesting fault scenarios (what happens when tank disconnects?)
- Threat cleanup at end of tick is O(entities * threat_table_size) but acceptable for MVP scale

---

## ADR-017: Control Channel Thread Safety — Command Queue Pattern

**Date:** 2026-02-23
**Status:** Accepted

**Context:** The control channel (Step 11) runs a TCP server on its own Asio network thread, accepting JSON commands to activate/deactivate faults at runtime. The `FaultRegistry` is NOT thread-safe — it's accessed by the game thread (`on_tick`, `execute_pre_tick_faults`). Direct calls from the control channel's network thread would be a data race.

**Decision:** Use a thread-safe `CommandQueue` (mutex + swap drain) as the sole shared state between the control channel's network thread and the game thread. The pattern mirrors `EventQueue` from ADR-009:

1. **Network thread** parses incoming JSON lines, pushes `ControlCommand` structs (containing the parsed request and a response callback) into the `CommandQueue`
2. **Game thread** calls `process_pending_commands()` once per tick to drain the queue and execute commands against the `FaultRegistry`
3. **Response write-back** via the `on_complete` callback, which uses `asio::post()` to dispatch the response write back to the control channel's `io_context` (ensuring socket writes happen on the network thread)
4. **JSON parse errors** are handled directly on the network thread (no queue round-trip needed) since they don't touch `FaultRegistry`

Tick ordering: `control.process_pending_commands()` → `registry.on_tick(tick)` → `zone_manager.tick_all(tick)`

**Consequences:**
- `FaultRegistry` is only ever accessed by the game thread — no mutex needed on registry operations
- `CommandQueue` is the only shared state, protected by a single mutex with brief critical sections
- Same proven pattern as `EventQueue` — consistent codebase, easy to reason about
- Response latency is bounded by tick interval (~50ms at 20 Hz) — acceptable for operator commands
- `asio::post()` for response write-back ensures socket operations stay on the correct thread
- Command execution is deterministic — processed in FIFO order at a known point in the tick cycle

---

## ADR-018: Python Module Organization — wowsim Package with Pydantic Models

**Date:** 2026-02-23
**Status:** Accepted

**Context:** The Python tooling needs shared data models (telemetry entries, anomalies, summaries) that are reusable across the CLI, log parser, future health checker, fault trigger, and mock client. CLAUDE.md shows flat files in `tools/` (e.g., `tools/log_parser.py`) but the actual package structure uses `tools/wowsim/`. Need a clear organization that makes modules importable by all tools and tests.

**Decision:**
1. **Package structure:** All importable code lives inside `tools/wowsim/` (not flat in `tools/`). CLI entry point at `wowsim.cli`, shared models at `wowsim.models`, tool modules at `wowsim.<tool>`.
2. **Pydantic v2 models** for all data structures: `TelemetryEntry` (validated from JSON Lines), `LogSummary`, `Anomaly`, `ParseResult`. Pydantic provides JSON parsing, validation, and serialization — eliminating hand-written parsing code.
3. **Shared conftest.py** at `tests/python/conftest.py` provides fixtures (sample JSONL lines, temp log files, anomaly entries) reusable by all future test modules.
4. **Literal types** for constrained fields: `TelemetryEntry.type` is `Literal["metric", "event", "health", "error"]`, catching invalid telemetry at parse time.

**Consequences:**
- All Python tools share validated models — no ad-hoc dict parsing in individual tools
- Pydantic's `model_validate()` gives clear error messages for malformed telemetry
- `ParseResult.model_dump_json()` provides free JSON output for `--format json` CLI flag
- Test fixtures in conftest.py are shared across test modules, reducing duplication
- Future tools (health check, fault trigger, mock client) import from `wowsim.models` directly

---

## ADR-019: Fault Trigger Client — Async TCP with Pydantic Protocol

**Date:** 2026-02-24
**Status:** Accepted

**Context:** Step 13 needs a Python client for the C++ control channel (TCP, newline-delimited JSON) and a CLI surface (`wowsim inject-fault`) for operators to activate/deactivate faults at runtime. The client will also be reused by the future dashboard (Step 17) for live fault control.

**Decision:**
1. **Async core, sync CLI surface** (per ADR-010). `ControlClient` is an `async with`-compatible TCP client using `asyncio.open_connection`. Five sync wrapper functions (e.g., `activate_fault`) call `asyncio.run()` for CLI one-shot use.
2. **Pydantic v2 models** for the control channel protocol: 5 request models (`FaultActivateRequest`, `FaultDeactivateRequest`, etc.), `FaultInfo` for per-fault status, and `ControlResponse` as a generic response envelope. Models serialize via `model_dump()` and deserialize via `model_validate_json()`.
3. **Click subcommand group** replaces the `inject-fault` stub. Group-level `--host`/`--port` options, 5 subcommands (`activate`, `deactivate`, `deactivate-all`, `status`, `list`). Duration parsing (`5s` → 100 ticks, `100t` → 100 ticks) via `parse_duration()`.
4. **Mock TCP server fixture** in conftest.py for testing: threading-based `socketserver.TCPServer` on ephemeral port 0, canned responses by command type, records received requests for assertion.
5. **Error propagation:** `ControlClientError` raised on `success=false` responses. `OSError` for connection failures. Both caught in CLI and converted to `click.ClickException`.

**Consequences:**
- Async `ControlClient` is directly usable by the Textual dashboard (Step 17) without blocking the event loop
- Pydantic models enforce protocol correctness at both serialization and deserialization boundaries
- Mock server fixture enables fast, reliable unit tests without a running C++ server
- Duration parsing abstracts the tick-rate constant (20 Hz) from operators
- 20 pytest cases cover models, parsing, client commands, error handling, and CLI integration

---

## ADR-020: Health Check Reporter — Log-Based Analysis with Module Reuse

**Date:** 2026-02-24
**Status:** Accepted

**Context:** Step 14 needs a health check reporter that provides periodic server health summaries including player counts, tick rate stability, zone health, and anomaly detection. The C++ server already emits rich structured telemetry (JSONL), and the log parser (Step 12) and fault trigger client (Step 13) provide reusable analysis and control channel capabilities.

**Decision:**
1. **Log-based analysis, no C++ changes.** All health data is already in the telemetry log: tick metrics (game_loop), zone metrics/errors (zone), connection events (game_server). No new server endpoints or telemetry types needed.
2. **Pure computation functions** operating on `list[TelemetryEntry]`: `compute_tick_health()`, `compute_zone_health()`, `estimate_player_count()`, `determine_status()`. Separating computation from I/O enables comprehensive unit testing without network or file dependencies.
3. **Reuse existing modules:** `log_parser.parse_line()` for entry parsing, `log_parser.detect_anomalies()` for anomaly detection, `fault_trigger.list_all_faults()` for active fault status. No logic duplication.
4. **Three new Pydantic models** in `wowsim.models`: `TickHealth` (tick rate stats), `ZoneHealthSummary` (per-zone health), `HealthReport` (complete report). `HealthReport` composes `Anomaly` and `FaultInfo` from existing models.
5. **Three-tier status determination:** `critical` (any critical anomaly or CRASHED zone), `degraded` (warning anomalies, DEGRADED zone, or >10% overruns), `healthy` (otherwise).
6. **CLI with watch mode:** `wowsim health --log-file <path>` with `--watch`/`--interval` for continuous monitoring, `--format json` for machine-readable output, `--no-faults` to skip control channel queries.

**Consequences:**
- Zero C++ changes — health reporter is purely additive Python tooling
- Pure function core is trivially testable (20 pytest cases)
- Module reuse validates the shared model architecture from ADR-018
- Watch mode provides live monitoring without a full dashboard (useful before Step 17)
- `--format json` output enables piping to external monitoring tools
- `build_health_report()` orchestrator designed for reuse by the dashboard (Step 17)

---

## ADR-021: Mock Client Spawner — Async TCP with Weighted Traffic Generation

**Date:** 2026-02-24
**Status:** Accepted

**Context:** Step 15 needs a mock client spawner that can generate N concurrent simulated players sending WoW-realistic game traffic to the C++ server. The PRD requires 50+ concurrent clients without tick rate degradation. The server currently accepts TCP connections and discards all received data (read loop for disconnect detection only), so payloads should match the C++ event types for protocol groundwork even though they are not yet parsed.

**Decision:**
1. **Async TCP with asyncio.gather** for concurrency. Each `MockGameClient` uses `asyncio.open_connection` (mirroring `ControlClient` from ADR-019). N clients run concurrently via `asyncio.gather`, each in its own coroutine. Sync wrapper `run_spawn()` calls `asyncio.run()` for CLI use.
2. **Weighted traffic generation** with pure functions. Three action types matching C++ event types: movement (50%), spell_cast (30%), combat (20%). Payloads include all fields the C++ event classes expect (session_id, position, spell_id, cast_time_ticks, target, damage_type). Position tracking per client provides spatial coherence across movement events.
3. **Per-client failure isolation.** Each client captures its own `OSError` on connection failure, returning a `ClientResult` with `connected=False` and error message. `spawn_clients` aggregates all results regardless of individual failures.
4. **Three Pydantic models** in `wowsim.models`: `ClientConfig` (host, port, rate, duration), `ClientResult` (per-client outcome), `SpawnResult` (aggregate with client list). Enables `--format json` output and programmatic consumption.
5. **ThreadingTCPServer fixture** for tests. Plain `TCPServer` would block on a single connection; `ThreadingTCPServer` handles N concurrent mock clients correctly. Mirrors the existing `_MockControlServer` pattern from conftest.py.

**Consequences:**
- Async design scales to 50+ clients without thread explosion — bounded by OS socket limits
- Weighted traffic generation produces realistic I/O patterns for stress testing
- Pure generation functions are independently testable without network dependencies
- Payloads match C++ event types exactly, ready for protocol parsing when server adds message framing
- Per-client failure handling means partial connectivity doesn't abort the entire spawn
- 18 pytest cases cover models, traffic generation, client lifecycle, spawning, formatting, and CLI

---

## ADR-022: Session Lifecycle Bridge — Thread-Safe Event Queue

**Date:** 2026-02-24
**Status:** Accepted

**Context:** GameServer runs on a network thread, while ZoneManager is owned by the game thread. When clients connect or disconnect, the game thread needs to create or remove entities in the appropriate zones. Direct callbacks from the network thread would require thread-safe zone operations, violating the single-owner game thread model (ADR-002).

**Decision:** Add a `SessionEventQueue` following the existing `EventQueue`/`CommandQueue` pattern (mutex + swap drain). GameServer pushes `CONNECTED`/`DISCONNECTED` events from the network thread; the game loop drains at tick start and calls `zone_manager.assign_session()` / `remove_session()`.

1. **SessionEventQueue** — lightweight thread-safe queue (`push` from network thread, `drain` from game thread). Same swap-based bulk transfer pattern as EventQueue and CommandQueue.
2. **GameServer integration** — `set_session_event_queue(SessionEventQueue*)` with non-owning raw pointer (per project convention). Queue lives on the stack in `main()`. Null-safe: no crash if no queue is set.
3. **Round-robin zone assignment** — odd session_id → zone 1, even → zone 2. Simple, deterministic, balances load across two zones.

**Consequences:**
- All game state mutations remain on the game thread — no thread-safety concerns for ZoneManager or Zone
- Consistent with EventQueue (ADR-009) and CommandQueue (ADR-017) patterns — three queues, one pattern
- Session assignment happens at the start of the next tick after connection (max 50ms latency) — acceptable for a reliability demo
- Zone assignment policy is trivially changeable (round-robin today, load-based tomorrow)

---

## ADR-023: Dashboard Data Flow — Worker Threads for Sync I/O, Native Async for Control

**Date:** 2026-02-24
**Status:** Accepted

**Context:** The Textual TUI dashboard needs to display live server metrics (from JSONL log files and TCP health probes) alongside fault control (via the async ControlClient). Textual runs on an asyncio event loop, so blocking I/O would freeze the UI. Two data sources have different I/O characteristics: health data uses sync file I/O and sync TCP probes, while fault control uses the existing async `ControlClient`.

**Decision:** Use a dual data-fetching strategy:

1. **Health refresh** — `@work(thread=True)` runs sync functions (`read_recent_entries`, `compute_tick_health`, `check_server_reachable`, etc.) from `health_check.py` and `log_parser.py` in a Textual worker thread. Results are marshalled back to the main thread via `call_from_thread()` for UI updates.
2. **Fault queries** — `@work(exclusive=True)` runs the async `ControlClient` directly on the Textual event loop. No thread needed since `ControlClient` was designed for async use (ADR-019).
3. **Timestamp watermark** — `filter_new_entries()` tracks the last-seen entry timestamp to append only new entries to the scrolling `RichLog`, preventing duplicates without per-entry tracking.
4. **Pure formatting functions** — All display formatting extracted as standalone functions (`format_status_bar`, `format_tick_panel`, `format_event_line`, `status_to_style`, `fault_action_label`) testable without any Textual dependency.

**Consequences:**
- Health refresh cannot block the UI — worker thread handles all sync I/O
- Fault control reuses the async ControlClient designed in ADR-019 — no new protocol code
- Pure formatting functions have 16 unit tests with no Textual dependency
- Dashboard refresh interval is configurable (default 2s) via CLI `--refresh` option
- Textual CSS provides clean separation of layout from logic

---

## ADR-024: Hotfix Pipeline — Fault-as-Deployment with Staged Health Gates

**Date:** 2026-02-24
**Status:** Accepted

**Context:** The PRD requires a "simulated build-validate-canary-promote-rollback pipeline" with "automated health checks at each stage" and "rollback triggers based on telemetry thresholds." The server already has fault injection (activate/deactivate), health checking (telemetry-based), and a live dashboard. The pipeline needs to compose these existing tools into a deployment lifecycle without adding new C++ code.

**Decision:**
1. **Fault-as-deployment metaphor.** Activating a fault = deploying a change. Deactivating = deploying a fix. Rollback = reversing the action. This maps naturally to the existing fault system and makes the pipeline demonstrable with the live server.
2. **Five-stage pipeline:** BUILD (health gate: reachable + not critical) → VALIDATE (health snapshot) → CANARY (execute deploy, poll health) → PROMOTE (success) or ROLLBACK (reverse action). Abort on BUILD or VALIDATE failure.
3. **Pure function core** with thin I/O wrappers. Six pure gate/formatting functions for testability. Three I/O wrappers (`_get_health_report`, `_execute_deploy_action`, `_execute_rollback`) compose `health_check.build_health_report()` and `fault_trigger.activate_fault()`/`deactivate_fault()`. Orchestrator calls wrappers and gates.
4. **Monkeypatch testing strategy.** Orchestration tests replace I/O wrappers with deterministic mocks via `monkeypatch.setattr`. Canary durations set to 0.1s for fast tests. No real server or network in unit tests.
5. **Pydantic models** for configuration and results: `PipelineConfig`, `StageResult`, `PipelineResult`. Enables `--format json` CLI output and programmatic consumption.
6. **CLI integration:** `wowsim deploy --fault-id <id> --action activate|deactivate` with canary duration, poll interval, rollback threshold, fault params, and text/json output.

**Consequences:**
- Zero C++ changes — pipeline is purely additive Python tooling
- Composes all existing tools (health_check, fault_trigger, log_parser) validating the modular architecture
- 20 pytest cases cover models, gate functions, formatting, orchestration, and CLI
- Pipeline is demonstrable with the live server: activate a latency spike, watch canary detect it, observe rollback
- Sync orchestrator with `time.sleep()` polling is simple and sufficient — no async complexity needed for a deployment pipeline

---

## ADR-025: Advanced Fault Scenarios — Multi-Phase State Machines on Existing Framework

**Date:** 2026-02-24
**Status:** Accepted

**Context:** The PRD and CLAUDE.md require four advanced failure scenarios (F5-F8) that demonstrate multi-zone, cascading, and temporal failure patterns. The existing fault framework supports TICK_SCOPED faults that receive a `Zone*` in `on_tick()`, providing access to `add_entity()`, `remove_entity()`, `push_event()`, `entities()`, `zone_id()`, and `entity_count()`.

**Decision:** Implement all four advanced faults as TICK_SCOPED faults with internal multi-phase state machines. No framework changes needed — each fault uses `zone->zone_id()` to differentiate behavior across zones and internal state to track phases:

1. **F5 CascadingZoneFailureFault** — Phase 1: throw `std::runtime_error` in source zone (crashes via exception guard). Phase 2: flood target zone with synthetic events (reuses F3 pattern). Demonstrates cascading failure across zone boundaries.
2. **F6 SlowLeakFault** — Increment-and-sleep pattern with configurable step size and frequency. Simulates gradual degradation. Detection via linearly growing tick duration in telemetry.
3. **F7 SplitBrainFault** — Create phantom NPC entities in each zone, inject zone-dependent movement (odd=east, even=north). Same entity ID at different positions across zones = state divergence.
4. **F8 ThunderingHerdFault** — Phase 1: remove all PLAYER entities (NPCs preserved). Phase 2: re-add all stored players simultaneously after delay. Intentionally bypasses ZoneManager's session_zone_map to create realistic chaos.

**Consequences:**
- Zero framework changes — all four faults compose with existing `Fault` base class, `FaultRegistry`, control channel, and Python CLI
- Multi-phase state machines are self-contained within each fault class — no coupling between faults
- F5's exception throw is caught by the existing zone exception guard — zone goes CRASHED, other zones continue
- F7's phantom entities remain inert after deactivation (no events injected when inactive) — harmless cleanup
- F8 intentionally creates inconsistency between ZoneManager state and zone entities, producing "unknown session" errors that serve as the detection signal
- All faults configurable via JSON params, activatable via control channel at runtime
- 14 new GoogleTest cases (total 264) cover all four faults with no regressions
