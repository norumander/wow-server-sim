"""Tests for wowsim.health_check — health computation, reporting, and CLI."""

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path

import pytest

from wowsim.models import (
    Anomaly,
    HealthReport,
    TelemetryEntry,
    TickHealth,
    ZoneHealthSummary,
)


# ============================================================
# Group A: Health Models (3 tests)
# ============================================================


class TestTickHealthConstruction:
    """TickHealth fields are correct and overrun_pct is computed."""

    def test_fields_populated(self) -> None:
        tick = TickHealth(
            total_ticks=100,
            avg_duration_ms=3.5,
            max_duration_ms=12.0,
            min_duration_ms=2.1,
            overrun_count=2,
            overrun_pct=2.0,
        )
        assert tick.total_ticks == 100
        assert tick.avg_duration_ms == 3.5
        assert tick.max_duration_ms == 12.0
        assert tick.min_duration_ms == 2.1
        assert tick.overrun_count == 2
        assert tick.overrun_pct == 2.0


class TestZoneHealthSummaryConstruction:
    """ZoneHealthSummary has all fields populated."""

    def test_fields_populated(self) -> None:
        zone = ZoneHealthSummary(
            zone_id=1,
            state="ACTIVE",
            tick_count=100,
            error_count=0,
            avg_tick_duration_ms=3.2,
        )
        assert zone.zone_id == 1
        assert zone.state == "ACTIVE"
        assert zone.tick_count == 100
        assert zone.error_count == 0
        assert zone.avg_tick_duration_ms == 3.2


class TestHealthReportJsonRoundTrip:
    """HealthReport model_dump_json → model_validate_json preserves data."""

    def test_round_trip(self) -> None:
        report = HealthReport(
            timestamp=datetime(2026, 2, 24, 10, 30, 0, tzinfo=timezone.utc),
            status="healthy",
            server_reachable=True,
            tick=TickHealth(
                total_ticks=100,
                avg_duration_ms=3.5,
                max_duration_ms=12.0,
                min_duration_ms=2.1,
                overrun_count=2,
                overrun_pct=2.0,
            ),
            zones=[
                ZoneHealthSummary(
                    zone_id=1,
                    state="ACTIVE",
                    tick_count=100,
                    error_count=0,
                    avg_tick_duration_ms=3.2,
                )
            ],
            connected_players=5,
            anomalies=[],
            active_faults=[],
            error_count=0,
            uptime_ticks=100,
        )
        json_str = report.model_dump_json()
        restored = HealthReport.model_validate_json(json_str)
        assert restored.status == "healthy"
        assert restored.server_reachable is True
        assert restored.tick is not None
        assert restored.tick.total_ticks == 100
        assert len(restored.zones) == 1
        assert restored.zones[0].zone_id == 1
        assert restored.connected_players == 5


# ============================================================
# Group B: Tick Health Computation (3 tests)
# ============================================================

from wowsim.health_check import compute_tick_health


class TestComputeTickHealthNormal:
    """Normal ticks produce correct avg/max/min and 0 overruns."""

    def test_normal_ticks(self) -> None:
        entries = [
            TelemetryEntry(
                v=1,
                timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
                type="metric",
                component="game_loop",
                message="Tick completed",
                data={"tick": i, "duration_ms": 3.0 + i * 0.5, "overrun": False},
            )
            for i in range(5)
        ]
        result = compute_tick_health(entries)
        assert result is not None
        assert result.total_ticks == 5
        assert result.min_duration_ms == 3.0
        assert result.max_duration_ms == 5.0
        # avg = (3.0 + 3.5 + 4.0 + 4.5 + 5.0) / 5 = 4.0
        assert result.avg_duration_ms == pytest.approx(4.0)
        assert result.overrun_count == 0
        assert result.overrun_pct == 0.0


class TestComputeTickHealthWithOverruns:
    """Ticks with overrun=True produce correct overrun_pct."""

    def test_overruns(self) -> None:
        entries = [
            TelemetryEntry(
                v=1,
                timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
                type="metric",
                component="game_loop",
                message="Tick completed",
                data={"tick": i, "duration_ms": 5.0, "overrun": i < 2},
            )
            for i in range(10)
        ]
        result = compute_tick_health(entries)
        assert result is not None
        assert result.total_ticks == 10
        assert result.overrun_count == 2
        assert result.overrun_pct == pytest.approx(20.0)


class TestComputeTickHealthNoTicks:
    """No game_loop entries returns None."""

    def test_no_ticks(self) -> None:
        entries = [
            TelemetryEntry(
                v=1,
                timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
                type="event",
                component="game_server",
                message="Connection accepted",
                data={"session_id": 1},
            )
        ]
        result = compute_tick_health(entries)
        assert result is None
