"""Tests for the wowsim demo_runner module.

Groups A–C: pure functions (binary detection, telemetry, ServerProcess).
Group D: CLI integration.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest


# ---------------------------------------------------------------------------
# Group A: find_server_binary (4 tests)
# ---------------------------------------------------------------------------


class TestFindServerBinary:
    """find_server_binary checks candidate paths in priority order."""

    def test_finds_debug_exe(self, tmp_path: Path) -> None:
        from wowsim.demo_runner import find_server_binary

        (tmp_path / "build" / "Debug").mkdir(parents=True)
        binary = tmp_path / "build" / "Debug" / "wow-server-sim.exe"
        binary.write_text("fake")
        result = find_server_binary(tmp_path)
        assert result is not None
        assert result == binary

    def test_finds_build_exe(self, tmp_path: Path) -> None:
        from wowsim.demo_runner import find_server_binary

        (tmp_path / "build").mkdir(parents=True)
        binary = tmp_path / "build" / "wow-server-sim.exe"
        binary.write_text("fake")
        result = find_server_binary(tmp_path)
        assert result is not None
        assert result == binary

    def test_finds_unix_binary(self, tmp_path: Path) -> None:
        from wowsim.demo_runner import find_server_binary

        (tmp_path / "build").mkdir(parents=True)
        binary = tmp_path / "build" / "wow-server-sim"
        binary.write_text("fake")
        result = find_server_binary(tmp_path)
        assert result is not None
        assert result == binary

    def test_returns_none_when_missing(self, tmp_path: Path) -> None:
        from wowsim.demo_runner import find_server_binary

        result = find_server_binary(tmp_path)
        assert result is None


# ---------------------------------------------------------------------------
# Group B: Telemetry helpers (2 tests)
# ---------------------------------------------------------------------------


class TestTelemetryHelpers:
    """clean_telemetry and default_telemetry_path work correctly."""

    def test_clean_telemetry_removes_file(self, tmp_path: Path) -> None:
        from wowsim.demo_runner import clean_telemetry

        f = tmp_path / "telemetry.jsonl"
        f.write_text("old data\n")
        clean_telemetry(f)
        assert not f.exists()

    def test_clean_telemetry_noop_when_missing(self, tmp_path: Path) -> None:
        from wowsim.demo_runner import clean_telemetry

        f = tmp_path / "telemetry.jsonl"
        clean_telemetry(f)  # should not raise
        assert not f.exists()

    def test_default_telemetry_path(self, tmp_path: Path) -> None:
        from wowsim.demo_runner import default_telemetry_path

        result = default_telemetry_path(tmp_path)
        assert result == tmp_path / "telemetry.jsonl"


# ---------------------------------------------------------------------------
# Group C: ServerProcess (2 tests)
# ---------------------------------------------------------------------------


class TestServerProcess:
    """ServerProcess lifecycle — construction and running property."""

    def test_not_running_initially(self) -> None:
        from wowsim.demo_runner import ServerProcess

        sp = ServerProcess(Path("fake-binary"), Path("fake.jsonl"))
        assert sp.running is False

    def test_has_start_and_stop(self) -> None:
        from wowsim.demo_runner import ServerProcess

        sp = ServerProcess(Path("fake-binary"), Path("fake.jsonl"))
        assert callable(sp.start)
        assert callable(sp.stop)


# ---------------------------------------------------------------------------
# Group D: CLI integration (2 tests)
# ---------------------------------------------------------------------------


class TestDemoCLI:
    """wowsim demo CLI command is registered and shows help."""

    def test_demo_help(self) -> None:
        from click.testing import CliRunner

        from wowsim.cli import main

        runner = CliRunner()
        result = runner.invoke(main, ["demo", "--help"])
        assert result.exit_code == 0
        assert "demo" in result.output.lower() or "Launch" in result.output

    def test_demo_no_binary_errors(self, tmp_path: Path, monkeypatch) -> None:
        """demo command errors when no server binary found."""
        from click.testing import CliRunner

        from wowsim.cli import main

        # Point to empty tmp_path where no binary exists
        monkeypatch.chdir(tmp_path)
        runner = CliRunner()
        result = runner.invoke(main, ["demo", "--project-root", str(tmp_path)])
        assert result.exit_code != 0
        assert "not found" in result.output.lower() or "error" in result.output.lower()
