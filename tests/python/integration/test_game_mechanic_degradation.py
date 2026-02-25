"""Integration tests: fault injection degrades game-mechanic telemetry.

Proves that infrastructure faults (latency spike) cause measurable
degradation in player-visible game mechanics (cast success rate, GCD
block rate, DPS). Bridges the gap between infrastructure alerts and
game impact — the core of the WoW-aware SRE narrative.

Tests:
  17 — Baseline game mechanics produce healthy status
  18 — Fault-degraded game mechanics produce degraded status
  19 — Cast success rate drops between baseline and fault scenarios
  20 — Health report reflects game-mechanic degradation in formatted output
"""

from __future__ import annotations

from pathlib import Path

from wowsim.game_metrics import aggregate_game_mechanics
from wowsim.health_check import build_health_report, format_health_report
from wowsim.log_parser import parse_file


class TestBaselineGameMechanicsProduceHealthyStatus:
    """Healthy game traffic → healthy status with cast success >= 50%."""

    def test_baseline_healthy(
        self,
        mock_game_server: dict,
        baseline_game_mechanic_log: Path,
    ) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]

        report = build_health_report(
            log_path=baseline_game_mechanic_log,
            game_host=host,
            game_port=port,
            skip_faults=True,
        )
        assert report.status == "healthy"
        assert report.game_mechanics is not None
        assert report.game_mechanics.cast_metrics.cast_success_rate >= 0.5


class TestFaultDegradedGameMechanicsProduceDegradedStatus:
    """Latency spike → degraded status with cast success < 50%."""

    def test_fault_degraded(
        self,
        mock_game_server: dict,
        mock_control_server: dict,
        fault_degraded_game_mechanic_log: Path,
    ) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]

        report = build_health_report(
            log_path=fault_degraded_game_mechanic_log,
            game_host=host,
            game_port=port,
            skip_faults=True,
        )
        assert report.status == "degraded"
        assert report.game_mechanics is not None
        assert report.game_mechanics.cast_metrics.cast_success_rate < 0.5


class TestCastSuccessRateDropsBetweenBaselineAndFault:
    """Direct metric comparison: baseline vs degraded game mechanics."""

    def test_cast_success_drops(
        self,
        baseline_game_mechanic_log: Path,
        fault_degraded_game_mechanic_log: Path,
    ) -> None:
        baseline_entries = parse_file(baseline_game_mechanic_log)
        degraded_entries = parse_file(fault_degraded_game_mechanic_log)

        baseline = aggregate_game_mechanics(baseline_entries)
        degraded = aggregate_game_mechanics(degraded_entries)

        # Baseline is healthy
        assert baseline.cast_metrics.cast_success_rate > 0.7

        # Degraded is failing
        assert degraded.cast_metrics.cast_success_rate < 0.5

        # More interrupts under fault
        assert degraded.cast_metrics.casts_interrupted > baseline.cast_metrics.casts_interrupted

        # More GCD blocks under fault
        assert degraded.cast_metrics.gcd_blocked > baseline.cast_metrics.gcd_blocked


class TestHealthReportReflectsGameMechanicDegradation:
    """Formatted health report shows game-mechanic degradation details."""

    def test_report_format(
        self,
        mock_game_server: dict,
        fault_degraded_game_mechanic_log: Path,
    ) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]

        report = build_health_report(
            log_path=fault_degraded_game_mechanic_log,
            game_host=host,
            game_port=port,
            skip_faults=True,
        )
        text = format_health_report(report)

        # Report contains game mechanics section
        assert "Game Mechanics" in text

        # Cast success rate is visible
        assert "Cast success:" in text

        # Status is degraded
        assert "DEGRADED" in text

        # Latency anomaly is visible (from spike ticks)
        assert "latency_spike" in text
