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
