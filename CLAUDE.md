# CLAUDE.md — WoW Server Simulator

## Project Overview
A C++ game server with Python reliability tooling that demonstrates server reliability engineering skills for the WoW Server team at Blizzard. See `docs/PRD.md` for full requirements.

## Tech Stack
- **Server**: C++17, CMake, POSIX sockets (or Boost.Asio), nlohmann/json
- **Tooling**: Python 3.11+, Click CLI framework
- **Testing**: GoogleTest (C++), pytest (Python)
- **Dashboard**: Python curses (MVP) or Flask + WebSocket (polish)
- **Containerization**: Docker + docker-compose

## Project Structure
```
wow-server-sim/
├── CLAUDE.md
├── README.md
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── docs/
│   ├── PRD.md
│   ├── ARCHITECTURE.md        # Living architecture doc
│   ├── DECISIONS.md            # ADR log (Architecture Decision Records)
│   ├── CHANGELOG.md            # Auto-maintained changelog
│   └── diagrams/
├── src/
│   ├── server/
│   │   ├── main.cpp
│   │   ├── game_server.h / .cpp        # TCP listener, session routing
│   │   ├── game_loop.h / .cpp          # Fixed-rate tick loop
│   │   ├── session.h / .cpp            # Player session lifecycle
│   │   ├── events/
│   │   │   ├── event.h                 # Base event type
│   │   │   ├── movement.h / .cpp
│   │   │   ├── combat.h / .cpp         # Damage calc, threat table
│   │   │   └── spellcast.h / .cpp      # Cast times, GCD, interrupts
│   │   ├── world/
│   │   │   ├── zone.h / .cpp           # Zone/instance isolation
│   │   │   └── entity.h / .cpp         # Players, NPCs
│   │   ├── telemetry/
│   │   │   ├── logger.h / .cpp         # Structured JSON logging
│   │   │   └── counters.h / .cpp       # Performance counters
│   │   └── fault/
│   │       ├── injector.h / .cpp       # Fault injection framework
│   │       └── scenarios.h / .cpp      # Predefined failure modes
│   └── control/
│       └── control_channel.h / .cpp    # Separate TCP channel for fault injection
├── tools/
│   ├── cli.py                  # Main Click entrypoint
│   ├── log_parser.py           # Telemetry analysis
│   ├── fault_trigger.py        # Fault injection client
│   ├── health_check.py         # Server health reporter
│   ├── mock_client.py          # Simulated player traffic
│   └── dashboard.py            # Monitoring UI
├── tests/
│   ├── cpp/
│   │   ├── CMakeLists.txt
│   │   ├── test_game_loop.cpp
│   │   ├── test_session.cpp
│   │   ├── test_combat.cpp
│   │   ├── test_spellcast.cpp
│   │   ├── test_zone.cpp
│   │   └── test_fault_injection.cpp
│   └── python/
│       ├── conftest.py
│       ├── test_log_parser.py
│       ├── test_health_check.py
│       ├── test_mock_client.py
│       └── integration/
│           ├── test_connection_lifecycle.py
│           ├── test_fault_and_recovery.py
│           └── test_end_to_end.py
├── scripts/
│   ├── build.sh
│   ├── run_all_tests.sh
│   └── demo.sh                 # Full demo walkthrough script
└── .github/
    └── workflows/
        └── ci.yml
```

## Development Methodology

### Test-Driven Development (TDD) — Strict Red-Green-Refactor

Every feature follows this exact cycle. No exceptions.

1. **RED**: Write a failing test that defines the expected behavior. Commit: `test: add failing test for <behavior>`
2. **GREEN**: Write the minimum code to make the test pass. Commit: `feat: implement <behavior>`
3. **REFACTOR**: Clean up while keeping tests green. Commit: `refactor: clean up <component>`

**TDD Rules:**
- Never write production code without a failing test first
- Tests must actually fail before writing implementation (verify the red)
- Write the simplest possible code to pass the test — resist premature abstraction
- Run the full relevant test suite after each green step to catch regressions
- If you discover a bug, write a test that reproduces it before fixing it: `test: reproduce <bug>` then `fix: <bug description>`

**Test Quality Standards:**
- Tests must be independent — no shared mutable state between tests
- Use descriptive test names: `TEST(Combat, DamageCalculationAppliesArmorReduction)` not `TEST(Combat, Test1)`
- Each test should test ONE behavior
- Use arrange-act-assert pattern consistently
- Integration tests should have clear setup/teardown and not depend on test ordering
- Aim for >90% line coverage on server code, >85% on Python tooling

### Commit Discipline — Atomic Commits

Every commit must be the smallest logical unit of change that leaves the project in a valid state. This is non-negotiable.

**Commit Message Format:**
```
<type>: <concise description in imperative mood>

[optional body explaining WHY, not WHAT]

[optional footer with references]
```

**Types:**
- `feat`: New functionality
- `fix`: Bug fix
- `test`: Adding or updating tests (including the RED step of TDD)
- `refactor`: Code restructuring with no behavior change
- `docs`: Documentation updates
- `build`: Build system, dependencies, CI
- `perf`: Performance improvements
- `style`: Code formatting only (no logic changes)

**Atomic Commit Rules:**
- One concern per commit. Never mix a feature with a refactor or a test with a fix.
- If you need "and" in the commit message, it should probably be two commits.
- A `test:` commit (red) must be immediately followed by the corresponding `feat:` or `fix:` commit (green).
- Refactors get their own commit and must not change behavior (tests stay green).
- Documentation updates accompany features but in a separate commit.
- If a change touches both C++ and Python, consider whether they can be split.
- Build/config changes are separate from code changes.

**Examples of good atomic commits:**
```
test: add failing test for spell cast GCD enforcement
feat: implement global cooldown check in spellcast handler
refactor: extract cooldown timer to reusable component
docs: document spell cast system in ARCHITECTURE.md
test: add failing test for combat tick damage calculation
feat: implement armor-reduced damage in combat tick
test: reproduce crash when player disconnects mid-cast
fix: handle null session in spellcast completion callback
```

**Examples of BAD commits (never do this):**
```
feat: add combat system and refactor event handling and fix session bug
update stuff
WIP
feat: implement spell cast, combat, and movement systems
```

### Rolling Documentation

Documentation is a first-class deliverable, not an afterthought. Update docs as part of the development flow.

**Living Documents to Maintain:**

1. **`README.md`** — Always accurate. Updated when build steps, usage, or project scope changes.
   - Quick start (clone → build → run in under 2 min)
   - Architecture overview with diagram reference
   - Demo walkthrough instructions

2. **`docs/ARCHITECTURE.md`** — Updated whenever a component is added or modified.
   - Component descriptions with responsibilities
   - Data flow narrative
   - Key design patterns used and why
   - Concurrency model explanation

3. **`docs/DECISIONS.md`** — Append-only ADR (Architecture Decision Record) log.
   Format per entry:
   ```
   ## ADR-NNN: <Title>
   **Date:** YYYY-MM-DD
   **Status:** Accepted | Superseded by ADR-NNN
   **Context:** Why this decision was needed
   **Decision:** What we chose
   **Consequences:** Tradeoffs and implications
   ```
   Record decisions like: socket library choice, tick rate, telemetry format, test framework, fault injection approach.

4. **`docs/CHANGELOG.md`** — Updated with every feature or fix commit.
   ```
   ## [Unreleased]
   ### Added
   - Spell cast system with GCD enforcement (#commit)
   ### Fixed
   - Session cleanup on abrupt disconnect (#commit)
   ```

5. **Inline Code Documentation:**
   - C++ headers: brief doc comment on every public class and method
   - Python: docstrings on all public functions and classes
   - Complex algorithms get a "why" comment, not a "what" comment
   - WoW-specific mechanics get a comment explaining the real-game parallel

**Documentation Commit Pattern:**
After implementing a feature (test → feat → refactor cycle), always follow up with:
```
docs: update ARCHITECTURE.md with <component>
docs: add ADR-NNN for <decision>
docs: update CHANGELOG with <feature>
```

## Build & Test Commands

```bash
# C++ build
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc)

# C++ tests
cd build && ctest --output-on-failure

# Python tests
cd tools && python -m pytest tests/ -v

# Python integration tests (requires running server)
cd tools && python -m pytest tests/integration/ -v --server-host localhost --server-port 8080

# All tests
./scripts/run_all_tests.sh

# Run server
./build/wow-server-sim --config config.yaml

# Run mock clients
python tools/cli.py spawn-clients --count 10 --duration 60

# Inject fault
python tools/cli.py inject --fault latency-spike --duration 5s

# Health check
python tools/cli.py health --watch --interval 2
```

## Code Style & Conventions

### C++
- C++17 standard, enforce with `-std=c++17 -Wall -Wextra -Werror`
- Use `snake_case` for functions and variables, `PascalCase` for types/classes
- RAII for all resource management — no raw `new`/`delete`
- `std::unique_ptr` for ownership, raw pointers for non-owning references
- Prefer `std::string_view` for read-only string parameters
- Use `enum class` not plain `enum`
- Header guards: `#pragma once`
- Keep headers minimal — forward declare where possible, include in .cpp
- No `using namespace std;` in headers, fine in .cpp files

### Python
- Python 3.11+ with type hints on all function signatures
- Format with `black`, lint with `ruff`
- Use `dataclasses` or `pydantic` for data structures
- Async where beneficial (especially mock client spawner)
- Click for all CLI interfaces

### Both Languages
- No magic numbers — use named constants
- Error messages must be actionable: say what went wrong AND what to do about it
- Log levels: DEBUG for development tracing, INFO for operational events, WARN for degraded states, ERROR for failures requiring attention

## WoW Domain Modeling Notes

These simplifications are intentional — the goal is to demonstrate domain knowledge and reliability engineering, not build a real game server.

- **Tick rate**: 20 Hz (50ms per tick), matching WoW's actual server tick rate
- **GCD**: 1.5 seconds (30 ticks), the standard WoW global cooldown
- **Spell casts**: Variable cast time, interruptible, resolved on tick boundaries
- **Combat**: Simplified damage = base_damage * (1 - armor_mitigation), no crit/haste/secondary stats
- **Zones**: Isolated processing units — a crash in one zone should not affect others
- **Threat**: Simple threat table — damage done = threat generated (no modifiers)
- **Sessions**: Represent a player's authenticated connection with state machine: CONNECTING → AUTHENTICATING → IN_WORLD → DISCONNECTING

## Fault Injection Scenarios

### MVP Scenarios
| ID | Name | Description | Detection Signal |
|----|------|-------------|-----------------|
| F1 | Latency Spike | Add 200ms delay to tick processing | Tick rate drops below threshold |
| F2 | Session Crash | Force-terminate a random player session | Unexpected disconnect in logs |
| F3 | Event Queue Flood | Inject 10x normal event volume | Queue depth alert, processing lag |
| F4 | Memory Pressure | Allocate and hold large buffers | Memory usage counter spike |

### Polish Scenarios
| ID | Name | Description | Detection Signal |
|----|------|-------------|-----------------|
| F5 | Cascading Zone Failure | Crash zone 1, redistribute players to zone 2, overload zone 2 | Multi-zone alert pattern |
| F6 | Slow Leak | Increment tick processing time by 1ms every 100 ticks | Gradual tick rate degradation |
| F7 | Split Brain | Partition two zones so they can't sync state | State divergence detection |
| F8 | Thundering Herd | Disconnect all players simultaneously, reconnect all at once | Auth system overload |

## Implementation Order

Follow this sequence. Each step is a complete TDD cycle (red → green → refactor → docs).

### Phase 1: MVP (24 hours)

```
1. Project scaffolding (CMake, directory structure, CI skeleton)
2. Telemetry logger (start here — everything else depends on observable output)
3. Game loop with fixed tick rate (no networking yet, just the timing loop)
4. Player session state machine (in-memory, no networking)
5. TCP server accepting connections, creating sessions
6. Movement event processing on game tick
7. Spell cast system (cast time, GCD, interrupts)
8. Combat tick (damage calculation, basic threat)
9. Zone isolation (players assigned to zones, zone-level processing)
10. Fault injection framework (base class + F1-F4 scenarios)
11. Control channel (TCP) for runtime fault injection
12. Python log parser
13. Python fault injection CLI
14. Python health check reporter
15. Python mock client spawner
16. Integration tests: connect → play → inject fault → detect → recover
```

### Phase 2: Polish (1 week)

```
17. Monitoring dashboard (terminal UI first, web upgrade optional)
18. Hotfix pipeline simulation (build → validate → canary → promote/rollback)
19. Advanced failure scenarios (F5-F8)
20. Performance benchmarks (max players, tick stability under load)
21. Demo script (scripted walkthrough of full fault → diagnose → fix → deploy cycle)
22. Architecture diagram generation
23. README polish with GIFs/screenshots
24. Docker compose for one-command demo
```

## Quality Gates

Before considering any phase complete:

- [ ] All tests pass (`./scripts/run_all_tests.sh` exits 0)
- [ ] No compiler warnings (`-Wall -Wextra -Werror`)
- [ ] Python passes `ruff check` and `black --check`
- [ ] `docs/ARCHITECTURE.md` reflects current state
- [ ] `docs/CHANGELOG.md` is up to date
- [ ] `README.md` quick-start instructions actually work from a clean clone
- [ ] Every public C++ class/method has a doc comment
- [ ] Every Python public function has a docstring
- [ ] No TODO comments without a corresponding tracked issue

## Agentic Workflow Tips

- **Before starting any task**: Read the relevant test file first to understand existing coverage, then the source file. This prevents duplicate work and ensures awareness of edge cases already handled.
- **After completing a TDD cycle**: Run the full test suite, not just the new test. Catch regressions early.
- **When unsure about a design decision**: Write it up as an ADR in DECISIONS.md first. The act of writing it out often clarifies the right choice.
- **When a task feels too large for one commit**: Break it down. If implementing "spell cast system," that's at least 3 commits: test + basic cast, test + GCD enforcement, test + interrupt handling.
- **When debugging a test failure**: Check the telemetry log output first — it's the same tool the project is demonstrating, so eating our own dog food.
- **Resist the urge to refactor prematurely**: Get to green first, then refactor. The refactor commit should change zero test outcomes.
- **Keep the demo running**: From step 5 onward, the server should always be runnable. Every commit maintains a working system.
