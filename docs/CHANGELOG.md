# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
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
