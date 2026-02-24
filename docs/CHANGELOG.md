# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Event system foundation: `GameEvent` base class with `EventType` enum (`MOVEMENT`, `SPELL_CAST`, `COMBAT`), `event_type_to_string()` conversion, polymorphic ownership via `unique_ptr`
- Movement event processing (`wow::MovementEvent`, `wow::MovementProcessor`): position updates on game tick with last-wins semantics for multiple events per session, telemetry on position change and unknown-session warnings
- Thread-safe event queue (`wow::EventQueue`): mutex-protected push/drain with O(1) bulk transfer via swap, bridging network and game threads
- Entity system (`wow::Entity`, `wow::Position`): 3D position tracking keyed by session_id, Euclidean distance calculation, equality operators
- 21 GoogleTest cases for movement system covering Position, GameEvent, MovementEvent, Entity, EventQueue (including concurrent access), MovementProcessor, and tick integration
- TCP game server (`wow::GameServer`) with Asio async accept loop, dedicated network thread, connection registry with atomic count, and RAII start/stop lifecycle. Configurable port (0 for OS-assigned in tests)
- Connection wrapper (`wow::Connection`) bridging TCP sockets and Sessions via `enable_shared_from_this`. Async read loop for disconnect detection, session transition to DESTROYED on client close
- Telemetry events for server start/stop, connection accepted (with session_id and remote_endpoint), and client disconnected
- 23 GoogleTest cases for GameServer covering construction, lifecycle, connection acceptance, disconnect handling, telemetry emission, and edge cases
- Windows build fix: Asio SYSTEM includes, `_WIN32_WINNT=0x0A00`, ws2_32/mswsock linkage

- Player session state machine (`wow::Session`) with 6 states, 7 events, and 10-entry constexpr transition table. Auto-assigned unique session IDs, telemetry on valid/invalid transitions, non-copyable/movable ownership semantics
- 21 GoogleTest cases for session covering construction, all valid transitions, invalid transition rejection, telemetry emission, and string conversion
- Fixed-rate game loop (`wow::GameLoop`) with configurable tick rate (default 20 Hz / 50ms), sleep-for-remainder timing via `steady_clock`, overrun detection, background thread or blocking mode, and per-tick telemetry emission
- 20 GoogleTest cases for game loop covering construction, lifecycle, tick execution, telemetry, overrun detection, and timing accuracy
- Structured JSON telemetry logger (`wow::Logger` singleton) with configurable sinks (file, stdout, custom ostream), ISO 8601 timestamps with millisecond precision, and thread-safe writes
- Logger convenience API: `metric()`, `event()`, `health()`, `error()` wrappers with optional structured `data` payload
- 28 GoogleTest cases for telemetry logger covering schema compliance, type mapping, data handling, multi-line output, file I/O, and concurrent writes
- Server startup/shutdown telemetry events in `main.cpp`
- Cross-platform build support: MSVC (`/W4 /WX`) alongside GCC/Clang (`-Wall -Wextra -Werror`), conditional pthread linking
- Project scaffolding: directory structure, CMake build system, git initialization
- Root `CMakeLists.txt` with FetchContent for Asio, nlohmann/json, GoogleTest
- `src/server/main.cpp` — compilable server stub
- `tests/cpp/CMakeLists.txt` — GoogleTest harness (empty, TDD populates)
- Python tooling package (`tools/wowsim`) with Click CLI and placeholder subcommands
- `tools/pyproject.toml` — installable package with `wowsim` entry point
- Build scripts: `scripts/build.sh`, `scripts/setup_venv.sh`, `scripts/run_all_tests.sh`
- `Dockerfile` — multi-stage build (gcc builder + slim runtime with Python)
- `docker-compose.yml` — server service with game, control, and telemetry ports
- `.github/workflows/ci.yml` — Docker build, C++ tests, Python lint + tests
- `README.md` — quick start, architecture overview, docs links
- Documentation: PRD, ARCHITECTURE, DECISIONS (ADR-001 through ADR-012), CHANGELOG
- `.gitignore` for C++, Python, IDE, and OS artifacts
