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
