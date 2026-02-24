"""Tests for wowsim.benchmark — performance benchmark suite."""

from __future__ import annotations

import json

import pytest

from tests.python.conftest import _make_line


# ============================================================
# Group A: Benchmark Models (3 tests)
# ============================================================


class TestBenchmarkConfigDefaults:
    """BenchmarkConfig provides sensible defaults for optional fields."""

    def test_defaults(self) -> None:
        from wowsim.models import BenchmarkConfig

        config = BenchmarkConfig()
        assert config.game_host == "localhost"
        assert config.game_port == 8080
        assert config.log_file is None
        assert config.client_counts == [0, 10, 25, 50, 100]
        assert config.duration_seconds == 10.0
        assert config.actions_per_second == 2.0
        assert config.settle_seconds == 2.0
        assert config.max_avg_tick_ms == 50.0
        assert config.max_p99_tick_ms == 100.0
        assert config.max_overrun_pct == 5.0


class TestBenchmarkConfigCustomOverrides:
    """BenchmarkConfig accepts custom values for all fields."""

    def test_overrides(self) -> None:
        from wowsim.models import BenchmarkConfig

        config = BenchmarkConfig(
            game_host="10.0.0.1",
            game_port=9090,
            log_file="test.jsonl",
            client_counts=[0, 5, 10],
            duration_seconds=5.0,
            actions_per_second=4.0,
            settle_seconds=1.0,
            max_avg_tick_ms=25.0,
            max_p99_tick_ms=50.0,
            max_overrun_pct=2.0,
        )
        assert config.game_host == "10.0.0.1"
        assert config.game_port == 9090
        assert config.log_file == "test.jsonl"
        assert config.client_counts == [0, 5, 10]
        assert config.duration_seconds == 5.0
        assert config.actions_per_second == 4.0
        assert config.settle_seconds == 1.0
        assert config.max_avg_tick_ms == 25.0
        assert config.max_p99_tick_ms == 50.0
        assert config.max_overrun_pct == 2.0


class TestBenchmarkModelsJsonRoundTrip:
    """BenchmarkResult serializes to JSON and deserializes back."""

    def test_round_trip(self) -> None:
        from wowsim.models import (
            BenchmarkConfig,
            BenchmarkResult,
            PercentileStats,
            ScenarioResult,
            TickHealth,
        )

        config = BenchmarkConfig()
        tick = TickHealth(
            total_ticks=200,
            avg_duration_ms=3.5,
            max_duration_ms=8.0,
            min_duration_ms=1.0,
            overrun_count=0,
            overrun_pct=0.0,
        )
        percentiles = PercentileStats(
            p50_ms=3.5, p95_ms=6.0, p99_ms=8.0, jitter_ms=1.2
        )
        scenario = ScenarioResult(
            client_count=50,
            tick_health=tick,
            percentiles=percentiles,
            throughput_actions_per_sec=100.0,
            passed=True,
            message="All thresholds met",
        )
        result = BenchmarkResult(
            config=config,
            scenarios=[scenario],
            overall_passed=True,
            summary_message="All scenarios passed",
            total_duration_seconds=30.0,
        )
        json_str = result.model_dump_json()
        restored = BenchmarkResult.model_validate_json(json_str)
        assert restored.overall_passed is True
        assert len(restored.scenarios) == 1
        assert restored.scenarios[0].client_count == 50
        assert restored.scenarios[0].percentiles.p99_ms == 8.0
        assert restored.config.max_avg_tick_ms == 50.0
        assert restored.total_duration_seconds == 30.0


# ============================================================
# Group B: Percentile Computation (3 tests)
# ============================================================


def _make_tick_entries(durations: list[float]) -> list:
    """Build parsed TelemetryEntry objects from a list of tick durations."""
    from wowsim.log_parser import parse_line

    entries = []
    for i, d in enumerate(durations):
        line = _make_line(
            "metric",
            "game_loop",
            "Tick completed",
            {"tick": i + 1, "duration_ms": d, "overrun": d > 50.0},
            f"2026-02-24T10:00:00.{i:03d}Z",
        )
        entry = parse_line(line)
        if entry is not None:
            entries.append(entry)
    return entries


class TestPercentilesKnownDistribution:
    """compute_percentiles returns correct P50/P95/P99 for a known distribution."""

    def test_known_distribution(self) -> None:
        from wowsim.benchmark import compute_percentiles

        # 100 values: 1.0, 2.0, ..., 100.0
        durations = [float(i) for i in range(1, 101)]
        entries = _make_tick_entries(durations)
        result = compute_percentiles(entries)
        assert result is not None
        assert result.p50_ms == 50.0
        assert result.p95_ms == 95.0
        assert result.p99_ms == 99.0
        assert result.jitter_ms > 0.0


class TestPercentilesConstantJitter:
    """Constant tick durations produce jitter_ms of 0.0."""

    def test_constant_jitter(self) -> None:
        from wowsim.benchmark import compute_percentiles

        durations = [5.0] * 50
        entries = _make_tick_entries(durations)
        result = compute_percentiles(entries)
        assert result is not None
        assert result.p50_ms == 5.0
        assert result.p95_ms == 5.0
        assert result.p99_ms == 5.0
        assert result.jitter_ms == 0.0


class TestPercentilesNoTicks:
    """No tick entries returns None."""

    def test_no_ticks(self) -> None:
        from wowsim.benchmark import compute_percentiles

        result = compute_percentiles([])
        assert result is None
