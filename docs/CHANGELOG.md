# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
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
