"""Tests for the wowsim dashboard module.

Groups A–E: pure formatting functions.
Groups F–H: event filtering, config, CLI integration.
"""

from __future__ import annotations

from datetime import datetime, timezone

import pytest

from wowsim.models import TelemetryEntry, TickHealth


# ---------------------------------------------------------------------------
# Group A: Status bar formatting (2 tests)
# ---------------------------------------------------------------------------


class TestFormatStatusBar:
    """format_status_bar produces a Rich-markup status line."""

    def test_healthy_reachable(self) -> None:
        from wowsim.dashboard import format_status_bar

        result = format_status_bar(
            status="healthy", reachable=True, players=12, uptime=1024
        )
        assert "HEALTHY" in result
        assert "reachable" in result
        assert "12" in result
        assert "1,024" in result or "1024" in result

    def test_critical_unreachable(self) -> None:
        from wowsim.dashboard import format_status_bar

        result = format_status_bar(
            status="critical", reachable=False, players=0, uptime=0
        )
        assert "CRITICAL" in result
        assert "unreachable" in result
        assert "0" in result


# ---------------------------------------------------------------------------
# Group B: Tick panel formatting (2 tests)
# ---------------------------------------------------------------------------


class TestFormatTickPanel:
    """format_tick_panel renders multi-line tick stats."""

    def test_normal_tick_health(self) -> None:
        from wowsim.dashboard import format_tick_panel

        tick = TickHealth(
            total_ticks=100,
            avg_duration_ms=3.5,
            max_duration_ms=12.0,
            min_duration_ms=2.1,
            overrun_count=2,
            overrun_pct=2.0,
        )
        result = format_tick_panel(tick)
        assert "3.5" in result
        assert "12.0" in result
        assert "2.1" in result
        assert "2" in result
        assert "2.0%" in result

    def test_none_tick_health(self) -> None:
        from wowsim.dashboard import format_tick_panel

        result = format_tick_panel(None)
        assert "no data" in result.lower() or "No data" in result


# ---------------------------------------------------------------------------
# Group C: Style mapping (1 test)
# ---------------------------------------------------------------------------


class TestStatusToStyle:
    """status_to_style maps status strings to Rich style strings."""

    def test_all_statuses(self) -> None:
        from wowsim.dashboard import status_to_style

        assert "green" in status_to_style("healthy")
        assert "yellow" in status_to_style("degraded")
        assert "red" in status_to_style("critical")


# ---------------------------------------------------------------------------
# Group D: Event line formatting (2 tests)
# ---------------------------------------------------------------------------


class TestFormatEventLine:
    """format_event_line renders a single telemetry entry as a compact log line."""

    def test_metric_entry(self) -> None:
        from wowsim.dashboard import format_event_line

        entry = TelemetryEntry(
            v=1,
            timestamp=datetime(2026, 2, 24, 19, 17, 10, tzinfo=timezone.utc),
            type="metric",
            component="zone",
            message="Zone tick completed",
        )
        result = format_event_line(entry)
        assert "19:17:10" in result
        assert "metric" in result
        assert "zone" in result
        assert "Zone tick completed" in result

    def test_error_entry(self) -> None:
        from wowsim.dashboard import format_event_line

        entry = TelemetryEntry(
            v=1,
            timestamp=datetime(2026, 2, 24, 10, 0, 0, 150000, tzinfo=timezone.utc),
            type="error",
            component="zone",
            message="Zone tick exception",
        )
        result = format_event_line(entry)
        assert "error" in result
        assert "Zone tick exception" in result


# ---------------------------------------------------------------------------
# Group E: Fault action label helper (2 tests)
# ---------------------------------------------------------------------------


class TestFaultActionLabel:
    """fault_action_label returns appropriate button label."""

    def test_active_fault(self) -> None:
        from wowsim.dashboard import fault_action_label

        assert fault_action_label(True) == "Deactivate"

    def test_inactive_fault(self) -> None:
        from wowsim.dashboard import fault_action_label

        assert fault_action_label(False) == "Activate"


# ---------------------------------------------------------------------------
# Group F: New entry filtering (3 tests)
# ---------------------------------------------------------------------------


class TestFilterNewEntries:
    """filter_new_entries applies timestamp watermark to avoid duplicates."""

    def test_first_load_returns_last_20(self, health_log_entries) -> None:
        """With no prior watermark, return the last 20 entries (or all if < 20)."""
        from wowsim.dashboard import filter_new_entries

        entries, new_ts = filter_new_entries(health_log_entries, last_ts=None)
        # health_log_entries has 11 entries, all should be returned
        assert len(entries) == 11
        assert new_ts is not None
        # watermark should be the timestamp of the last entry
        assert new_ts == health_log_entries[-1].timestamp

    def test_subsequent_load_filters_by_watermark(self) -> None:
        """Only entries newer than last_ts are returned."""
        from wowsim.dashboard import filter_new_entries

        ts1 = datetime(2026, 2, 24, 10, 0, 0, tzinfo=timezone.utc)
        ts2 = datetime(2026, 2, 24, 10, 0, 1, tzinfo=timezone.utc)
        ts3 = datetime(2026, 2, 24, 10, 0, 2, tzinfo=timezone.utc)

        entries = [
            TelemetryEntry(v=1, timestamp=ts1, type="event", component="a", message="m1"),
            TelemetryEntry(v=1, timestamp=ts2, type="event", component="b", message="m2"),
            TelemetryEntry(v=1, timestamp=ts3, type="event", component="c", message="m3"),
        ]
        result, new_ts = filter_new_entries(entries, last_ts=ts1)
        assert len(result) == 2
        assert result[0].component == "b"
        assert result[1].component == "c"
        assert new_ts == ts3

    def test_empty_entries(self) -> None:
        """Empty entry list returns empty list and None watermark."""
        from wowsim.dashboard import filter_new_entries

        entries, new_ts = filter_new_entries([], last_ts=None)
        assert entries == []
        assert new_ts is None


# ---------------------------------------------------------------------------
# Group G: DashboardConfig (2 tests)
# ---------------------------------------------------------------------------


class TestDashboardConfig:
    """DashboardConfig holds connection and display parameters."""

    def test_defaults(self, tmp_path) -> None:
        from wowsim.dashboard import DashboardConfig

        log_file = tmp_path / "test.jsonl"
        log_file.write_text("")
        config = DashboardConfig(log_file=log_file)
        assert config.host == "localhost"
        assert config.port == 8080
        assert config.control_port == 8081
        assert config.refresh_interval == 2.0

    def test_custom_overrides(self, tmp_path) -> None:
        from wowsim.dashboard import DashboardConfig

        log_file = tmp_path / "test.jsonl"
        log_file.write_text("")
        config = DashboardConfig(
            log_file=log_file,
            host="10.0.0.1",
            port=9090,
            control_port=9091,
            refresh_interval=5.0,
        )
        assert config.host == "10.0.0.1"
        assert config.port == 9090
        assert config.control_port == 9091
        assert config.refresh_interval == 5.0


# ---------------------------------------------------------------------------
# Group H: CLI integration (2 tests)
# ---------------------------------------------------------------------------


class TestCLI:
    """CLI dashboard command is wired and validates options."""

    def test_missing_log_file_errors(self) -> None:
        from click.testing import CliRunner

        from wowsim.cli import main

        runner = CliRunner()
        result = runner.invoke(main, ["dashboard"])
        assert result.exit_code != 0
        assert "log-file" in result.output.lower() or "log_file" in result.output.lower() or "Missing" in result.output or "required" in result.output.lower()

    def test_help_shows_options(self) -> None:
        from click.testing import CliRunner

        from wowsim.cli import main

        runner = CliRunner()
        result = runner.invoke(main, ["dashboard", "--help"])
        assert result.exit_code == 0
        assert "--log-file" in result.output
        assert "--host" in result.output
        assert "--refresh" in result.output


# ---------------------------------------------------------------------------
# Group I: Suggestion bar (6 tests)
# ---------------------------------------------------------------------------


class TestComputeSuggestion:
    """compute_suggestion returns context-aware guidance text."""

    def test_no_players(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(players=0, active_faults=0, status="healthy", pipeline_ran=False)
        assert "s" in result.lower()
        assert "spawn" in result.lower() or "player" in result.lower()

    def test_players_no_faults(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(players=5, active_faults=0, status="healthy", pipeline_ran=False)
        assert "a" in result.lower()
        assert "fault" in result.lower() or "inject" in result.lower()

    def test_fault_active_critical(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(players=5, active_faults=1, status="critical", pipeline_ran=False)
        assert "d" in result.lower()
        assert "deactivate" in result.lower() or "recover" in result.lower()

    def test_fault_active_not_critical(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(players=5, active_faults=1, status="degraded", pipeline_ran=False)
        assert "observe" in result.lower() or "active" in result.lower()

    def test_no_faults_not_critical_no_pipeline(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(players=5, active_faults=0, status="healthy", pipeline_ran=False)
        # With players and no faults, should suggest either fault injection or pipeline
        assert "a" in result.lower() or "p" in result.lower()

    def test_pipeline_ran(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(players=5, active_faults=0, status="healthy", pipeline_ran=True)
        assert "s" in result.lower() or "q" in result.lower()


# ---------------------------------------------------------------------------
# Group J: Spawn clients keybinding (2 tests)
# ---------------------------------------------------------------------------


class TestSpawnClientsBinding:
    """Dashboard has 's' keybinding that triggers spawn action."""

    def test_binding_registered(self) -> None:
        from wowsim.dashboard import WoWDashboardApp

        keys = [b[0] if isinstance(b, tuple) else b.key for b in WoWDashboardApp.BINDINGS]
        assert "s" in keys

    def test_action_method_exists(self) -> None:
        from wowsim.dashboard import WoWDashboardApp

        assert hasattr(WoWDashboardApp, "action_spawn_clients")
        assert callable(getattr(WoWDashboardApp, "action_spawn_clients"))


# ---------------------------------------------------------------------------
# Group K: Fault catalog and format_fault_option (3 tests)
# ---------------------------------------------------------------------------


class TestFaultCatalog:
    """FAULT_CATALOG contains all 8 fault scenarios with descriptions."""

    def test_catalog_has_all_faults(self) -> None:
        from wowsim.dashboard import FAULT_CATALOG

        expected = {
            "latency-spike",
            "session-crash",
            "event-queue-flood",
            "memory-pressure",
            "cascading-zone-failure",
            "slow-leak",
            "split-brain",
            "thundering-herd",
        }
        assert set(FAULT_CATALOG.keys()) == expected

    def test_each_fault_has_description(self) -> None:
        from wowsim.dashboard import FAULT_CATALOG

        for fault_id, info in FAULT_CATALOG.items():
            assert "description" in info, f"{fault_id} missing description"
            assert len(info["description"]) > 0

    def test_format_fault_option(self) -> None:
        from wowsim.dashboard import format_fault_option

        result = format_fault_option("latency-spike", "Add 200ms delay")
        assert "latency-spike" in result
        assert "200ms" in result


# ---------------------------------------------------------------------------
# Group L: Pipeline keybinding (2 tests)
# ---------------------------------------------------------------------------


class TestPipelineBinding:
    """Dashboard has 'p' keybinding that triggers pipeline action."""

    def test_binding_registered(self) -> None:
        from wowsim.dashboard import WoWDashboardApp

        keys = [b[0] if isinstance(b, tuple) else b.key for b in WoWDashboardApp.BINDINGS]
        assert "p" in keys

    def test_action_method_exists(self) -> None:
        from wowsim.dashboard import WoWDashboardApp

        assert hasattr(WoWDashboardApp, "action_run_pipeline")
        assert callable(getattr(WoWDashboardApp, "action_run_pipeline"))


# ---------------------------------------------------------------------------
# Group M: Spawn active suggestion (3 tests)
# ---------------------------------------------------------------------------


class TestSpawnActiveSuggestion:
    """compute_suggestion with spawn_active=True returns despawn guidance."""

    def test_spawn_active_shows_despawn_hint(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(
            players=5, active_faults=0, status="healthy",
            pipeline_ran=False, spawn_active=True,
        )
        assert "k" in result.lower()
        assert "despawn" in result.lower() or "active" in result.lower()

    def test_spawn_active_overrides_normal_suggestion(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(
            players=0, active_faults=0, status="healthy",
            pipeline_ran=False, spawn_active=True,
        )
        # Even with 0 players, spawn_active takes priority
        assert "k" in result.lower()

    def test_spawn_not_active_preserves_existing(self) -> None:
        from wowsim.dashboard import compute_suggestion

        result = compute_suggestion(
            players=0, active_faults=0, status="healthy",
            pipeline_ran=False, spawn_active=False,
        )
        assert "s" in result.lower()
        assert "spawn" in result.lower() or "player" in result.lower()


# ---------------------------------------------------------------------------
# Group N: Despawn keybinding (2 tests)
# ---------------------------------------------------------------------------


class TestDespawnBinding:
    """Dashboard has 'k' keybinding that triggers despawn action."""

    def test_binding_registered(self) -> None:
        from wowsim.dashboard import WoWDashboardApp

        keys = [b[0] if isinstance(b, tuple) else b.key for b in WoWDashboardApp.BINDINGS]
        assert "k" in keys

    def test_action_method_exists(self) -> None:
        from wowsim.dashboard import WoWDashboardApp

        assert hasattr(WoWDashboardApp, "action_despawn_clients")
        assert callable(getattr(WoWDashboardApp, "action_despawn_clients"))


# ---------------------------------------------------------------------------
# Group O: Duration picker and options (3 tests)
# ---------------------------------------------------------------------------


class TestDurationPicker:
    """DURATION_OPTIONS constant and DurationPickerScreen exist."""

    def test_duration_options_exist(self) -> None:
        from wowsim.dashboard import DURATION_OPTIONS

        assert len(DURATION_OPTIONS) == 4

    def test_duration_options_include_persistent(self) -> None:
        from wowsim.dashboard import DURATION_OPTIONS

        durations = [d[1] for d in DURATION_OPTIONS]
        assert float("inf") in durations

    def test_duration_picker_screen_exists(self) -> None:
        from wowsim.dashboard import DurationPickerScreen

        assert DurationPickerScreen is not None
