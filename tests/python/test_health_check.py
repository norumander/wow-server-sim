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


# ============================================================
# Group C: Zone Health & Player Count (3 tests)
# ============================================================

from wowsim.health_check import compute_zone_health, estimate_player_count


class TestComputeZoneHealthMultipleZones:
    """Two zones produce correct per-zone stats."""

    def test_multiple_zones(self, health_log_entries: list[TelemetryEntry]) -> None:
        zones = compute_zone_health(health_log_entries)
        assert len(zones) == 2
        zone_map = {z.zone_id: z for z in zones}
        assert 1 in zone_map
        assert 2 in zone_map
        assert zone_map[1].tick_count == 1
        assert zone_map[2].tick_count == 1
        assert zone_map[1].avg_tick_duration_ms == pytest.approx(3.2)
        assert zone_map[2].avg_tick_duration_ms == pytest.approx(2.8)


class TestComputeZoneHealthWithErrors:
    """Zone exceptions produce correct error_count and CRASHED state."""

    def test_zone_errors(self, health_log_entries: list[TelemetryEntry]) -> None:
        zones = compute_zone_health(health_log_entries)
        zone_map = {z.zone_id: z for z in zones}
        # Zone 1 has 1 error → CRASHED
        assert zone_map[1].error_count == 1
        assert zone_map[1].state == "CRASHED"
        # Zone 2 has 0 errors → ACTIVE
        assert zone_map[2].error_count == 0
        assert zone_map[2].state == "ACTIVE"


class TestEstimatePlayerCount:
    """Net connections - disconnections = player count."""

    def test_player_count(self, health_log_entries: list[TelemetryEntry]) -> None:
        # 2 connections, 1 disconnect → 1
        count = estimate_player_count(health_log_entries)
        assert count == 1


# ============================================================
# Group D: Status Determination (3 tests)
# ============================================================

from wowsim.health_check import determine_status


class TestStatusHealthy:
    """No anomalies, all zones ACTIVE → 'healthy'."""

    def test_healthy(self) -> None:
        zones = [
            ZoneHealthSummary(
                zone_id=1, state="ACTIVE", tick_count=10,
                error_count=0, avg_tick_duration_ms=3.0,
            ),
        ]
        status = determine_status(
            tick=TickHealth(
                total_ticks=100, avg_duration_ms=3.5, max_duration_ms=5.0,
                min_duration_ms=2.0, overrun_count=0, overrun_pct=0.0,
            ),
            zones=zones,
            anomalies=[],
        )
        assert status == "healthy"


class TestStatusDegraded:
    """Warning anomalies or DEGRADED zone → 'degraded'."""

    def test_degraded_from_warning_anomaly(self) -> None:
        anomalies = [
            Anomaly(
                type="latency_spike",
                severity="warning",
                timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
                message="Tick slow",
            ),
        ]
        status = determine_status(tick=None, zones=[], anomalies=anomalies)
        assert status == "degraded"

    def test_degraded_from_overrun_pct(self) -> None:
        tick = TickHealth(
            total_ticks=100, avg_duration_ms=3.5, max_duration_ms=5.0,
            min_duration_ms=2.0, overrun_count=15, overrun_pct=15.0,
        )
        status = determine_status(tick=tick, zones=[], anomalies=[])
        assert status == "degraded"


class TestStatusCritical:
    """Critical anomaly or CRASHED zone → 'critical'."""

    def test_critical_from_anomaly(self) -> None:
        anomalies = [
            Anomaly(
                type="zone_crash",
                severity="critical",
                timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
                message="Zone 1 crashed",
            ),
        ]
        status = determine_status(tick=None, zones=[], anomalies=anomalies)
        assert status == "critical"

    def test_critical_from_crashed_zone(self) -> None:
        zones = [
            ZoneHealthSummary(
                zone_id=1, state="CRASHED", tick_count=10,
                error_count=3, avg_tick_duration_ms=50.0,
            ),
        ]
        status = determine_status(tick=None, zones=zones, anomalies=[])
        assert status == "critical"


# ============================================================
# Group D2: Game-Mechanic Health Signals (4 tests)
# ============================================================


class TestStatusDegradedFromHighGCDBlockRate:
    """GCD block rate > 50% triggers 'degraded'."""

    def test_high_gcd_block_rate(self) -> None:
        from wowsim.models import CastMetrics, CombatMetrics, GameMechanicSummary

        gm = GameMechanicSummary(
            cast_metrics=CastMetrics(
                casts_started=5,
                casts_completed=3,
                casts_interrupted=2,
                gcd_blocked=10,
                cast_success_rate=0.6,
                gcd_block_rate=0.667,  # > 50%
                cast_rate_per_sec=1.0,
            ),
            combat_metrics=CombatMetrics(
                total_damage=1000, total_attacks=5, kills=0,
                active_entities=1, overall_dps=100.0,
            ),
            top_damage_dealers=[],
            duration_seconds=10.0,
        )
        status = determine_status(tick=None, zones=[], anomalies=[], game_mechanics=gm)
        assert status == "degraded"


class TestStatusDegradedFromLowCastSuccessRate:
    """Cast success rate < 50% triggers 'degraded'."""

    def test_low_cast_success_rate(self) -> None:
        from wowsim.models import CastMetrics, CombatMetrics, GameMechanicSummary

        gm = GameMechanicSummary(
            cast_metrics=CastMetrics(
                casts_started=10,
                casts_completed=3,
                casts_interrupted=7,
                gcd_blocked=0,
                cast_success_rate=0.3,  # < 50%
                gcd_block_rate=0.0,
                cast_rate_per_sec=1.0,
            ),
            combat_metrics=CombatMetrics(
                total_damage=1000, total_attacks=5, kills=0,
                active_entities=1, overall_dps=100.0,
            ),
            top_damage_dealers=[],
            duration_seconds=10.0,
        )
        status = determine_status(tick=None, zones=[], anomalies=[], game_mechanics=gm)
        assert status == "degraded"


class TestStatusDegradedFromZeroCombatWithPlayers:
    """Zero combat activity with connected players triggers 'degraded'."""

    def test_zero_combat_with_players(self) -> None:
        from wowsim.models import CastMetrics, CombatMetrics, GameMechanicSummary

        gm = GameMechanicSummary(
            cast_metrics=CastMetrics(
                casts_started=0, casts_completed=0, casts_interrupted=0,
                gcd_blocked=0, cast_success_rate=0.0,
                gcd_block_rate=0.0, cast_rate_per_sec=0.0,
            ),
            combat_metrics=CombatMetrics(
                total_damage=0, total_attacks=0, kills=0,
                active_entities=0, overall_dps=0.0,
            ),
            top_damage_dealers=[],
            duration_seconds=10.0,
        )
        status = determine_status(
            tick=None, zones=[], anomalies=[],
            game_mechanics=gm, connected_players=5,
        )
        assert status == "degraded"


class TestStatusHealthyWithGoodGameMechanics:
    """Healthy game mechanics should not affect healthy status."""

    def test_good_mechanics_stay_healthy(self) -> None:
        from wowsim.models import CastMetrics, CombatMetrics, GameMechanicSummary

        gm = GameMechanicSummary(
            cast_metrics=CastMetrics(
                casts_started=10, casts_completed=9, casts_interrupted=1,
                gcd_blocked=1, cast_success_rate=0.9,
                gcd_block_rate=0.09, cast_rate_per_sec=1.0,
            ),
            combat_metrics=CombatMetrics(
                total_damage=5000, total_attacks=20, kills=2,
                active_entities=3, overall_dps=500.0,
            ),
            top_damage_dealers=[],
            duration_seconds=10.0,
        )
        status = determine_status(
            tick=None, zones=[], anomalies=[],
            game_mechanics=gm, connected_players=3,
        )
        assert status == "healthy"


# ============================================================
# Group E: Server Reachability (2 tests)
# ============================================================

from wowsim.health_check import check_server_reachable


class TestCheckServerReachableSuccess:
    """Connecting to mock_control_server returns True."""

    def test_reachable(self, mock_control_server: dict) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]
        assert check_server_reachable(host, port) is True


class TestCheckServerReachableFailure:
    """Connecting to a closed port returns False."""

    def test_unreachable(self) -> None:
        assert check_server_reachable("127.0.0.1", 1, timeout=0.5) is False


# ============================================================
# Group F: Report Building & Formatting (2 tests)
# ============================================================

from wowsim.health_check import build_health_report, format_health_report


class TestBuildHealthReportFromLog:
    """build_health_report with sample log file produces complete report."""

    def test_complete_report(self, health_log_file: Path) -> None:
        report = build_health_report(
            log_path=health_log_file,
            game_host="127.0.0.1",
            game_port=1,
            control_host="127.0.0.1",
            control_port=1,  # unreachable, intentionally
            skip_faults=True,
        )
        assert isinstance(report, HealthReport)
        assert report.server_reachable is False
        assert report.tick is not None
        assert report.tick.total_ticks == 5
        assert len(report.zones) == 2
        assert report.connected_players == 1
        assert report.error_count >= 1
        assert report.status in ("healthy", "degraded", "critical")


class TestFormatHealthReportText:
    """Formatted output contains key sections."""

    def test_format_contains_sections(self) -> None:
        report = HealthReport(
            timestamp=datetime(2026, 2, 24, 10, 30, 0, tzinfo=timezone.utc),
            status="healthy",
            server_reachable=True,
            tick=TickHealth(
                total_ticks=1000, avg_duration_ms=3.5, max_duration_ms=12.0,
                min_duration_ms=2.1, overrun_count=2, overrun_pct=0.2,
            ),
            zones=[
                ZoneHealthSummary(
                    zone_id=1, state="ACTIVE", tick_count=100,
                    error_count=0, avg_tick_duration_ms=3.2,
                ),
            ],
            connected_players=5,
        )
        text = format_health_report(report)
        assert "HEALTHY" in text
        assert "reachable" in text
        assert "Tick Rate" in text
        assert "1000" in text
        assert "Zone 1" in text
        assert "ACTIVE" in text
        assert "Connected Players: 5" in text
        assert "Active Faults: none" in text
        assert "Anomalies: none" in text


# ============================================================
# Group G: CLI Integration (2 tests)
# ============================================================


class TestFormatHealthReportWithGameMechanics:
    """Formatted output includes Game Mechanics section when present."""

    def test_format_includes_game_mechanics(self) -> None:
        from wowsim.models import CastMetrics, CombatMetrics, EntityDPS, GameMechanicSummary

        report = HealthReport(
            timestamp=datetime(2026, 2, 25, 10, 0, 0, tzinfo=timezone.utc),
            status="healthy",
            server_reachable=True,
            game_mechanics=GameMechanicSummary(
                cast_metrics=CastMetrics(
                    casts_started=10, casts_completed=8, casts_interrupted=2,
                    gcd_blocked=1, cast_success_rate=0.8,
                    gcd_block_rate=0.09, cast_rate_per_sec=1.0,
                ),
                combat_metrics=CombatMetrics(
                    total_damage=5000, total_attacks=15, kills=2,
                    active_entities=3, overall_dps=250.0,
                ),
                top_damage_dealers=[
                    EntityDPS(entity_id=1, total_damage=3000, dps=150.0, attack_count=8),
                ],
                duration_seconds=20.0,
            ),
        )
        text = format_health_report(report)
        assert "Game Mechanics" in text
        assert "80.0%" in text  # cast success rate
        assert "250.0" in text  # overall DPS


class TestCLIHealthWithLogFile:
    """CLI health command with valid log file exits 0 and shows status."""

    def test_cli_health(self, health_log_file: Path) -> None:
        from click.testing import CliRunner

        from wowsim.cli import main

        runner = CliRunner()
        result = runner.invoke(
            main,
            [
                "health",
                "--log-file", str(health_log_file),
                "--host", "127.0.0.1",
                "--port", "1",
                "--no-faults",
            ],
        )
        assert result.exit_code == 0, result.output
        assert "Status:" in result.output or "status" in result.output.lower()


class TestCLIHealthMissingLogFile:
    """CLI health with nonexistent log file shows error."""

    def test_missing_log_file(self, tmp_path: Path) -> None:
        from click.testing import CliRunner

        from wowsim.cli import main

        bad_path = tmp_path / "nonexistent.jsonl"
        runner = CliRunner()
        result = runner.invoke(
            main,
            ["health", "--log-file", str(bad_path), "--no-faults"],
        )
        assert result.exit_code != 0
