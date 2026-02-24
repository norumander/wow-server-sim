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


# ============================================================
# Group C: Throughput Computation (2 tests)
# ============================================================


class TestThroughputNormal:
    """compute_throughput returns actions/sec from a SpawnResult."""

    def test_normal(self) -> None:
        from wowsim.benchmark import compute_throughput
        from wowsim.models import SpawnResult

        result = SpawnResult(
            total_clients=10,
            successful_connections=10,
            failed_connections=0,
            total_actions_sent=100,
            total_duration_seconds=10.0,
            clients=[],
        )
        assert compute_throughput(result) == 10.0


class TestThroughputZeroDuration:
    """compute_throughput returns 0.0 when duration is zero."""

    def test_zero_duration(self) -> None:
        from wowsim.benchmark import compute_throughput
        from wowsim.models import SpawnResult

        result = SpawnResult(
            total_clients=0,
            successful_connections=0,
            failed_connections=0,
            total_actions_sent=0,
            total_duration_seconds=0.0,
            clients=[],
        )
        assert compute_throughput(result) == 0.0


# ============================================================
# Group D: Scenario Evaluation (3 tests)
# ============================================================


def _make_scenario_inputs(
    avg_ms: float = 3.5,
    p99_ms: float = 8.0,
    overrun_pct: float = 0.0,
    count: int = 50,
):
    """Build inputs for evaluate_scenario tests."""
    from wowsim.models import BenchmarkConfig, PercentileStats, TickHealth

    tick = TickHealth(
        total_ticks=200,
        avg_duration_ms=avg_ms,
        max_duration_ms=p99_ms,
        min_duration_ms=1.0,
        overrun_count=int(overrun_pct * 2),
        overrun_pct=overrun_pct,
    )
    percentiles = PercentileStats(
        p50_ms=avg_ms, p95_ms=p99_ms * 0.9, p99_ms=p99_ms, jitter_ms=1.0
    )
    config = BenchmarkConfig()
    return tick, percentiles, 100.0, count, config


class TestScenarioEvalAllPass:
    """evaluate_scenario passes when all thresholds are met."""

    def test_passes(self) -> None:
        from wowsim.benchmark import evaluate_scenario

        tick, percentiles, throughput, count, config = _make_scenario_inputs()
        result = evaluate_scenario(tick, percentiles, throughput, count, config)
        assert result.passed is True
        assert result.client_count == 50


class TestScenarioEvalFailAvg:
    """evaluate_scenario fails when avg tick exceeds threshold."""

    def test_fail_avg(self) -> None:
        from wowsim.benchmark import evaluate_scenario

        tick, percentiles, throughput, count, config = _make_scenario_inputs(
            avg_ms=60.0
        )
        result = evaluate_scenario(tick, percentiles, throughput, count, config)
        assert result.passed is False
        assert "avg" in result.message.lower()


class TestScenarioEvalFailP99:
    """evaluate_scenario fails when p99 tick exceeds threshold."""

    def test_fail_p99(self) -> None:
        from wowsim.benchmark import evaluate_scenario

        tick, percentiles, throughput, count, config = _make_scenario_inputs(
            p99_ms=150.0
        )
        result = evaluate_scenario(tick, percentiles, throughput, count, config)
        assert result.passed is False
        assert "p99" in result.message.lower()


# ============================================================
# Group E: Overall Evaluation (2 tests)
# ============================================================


class TestOverallEvalAllPass:
    """evaluate_benchmark passes when all scenarios pass."""

    def test_all_pass(self) -> None:
        from wowsim.benchmark import evaluate_benchmark
        from wowsim.models import PercentileStats, ScenarioResult, TickHealth

        tick = TickHealth(
            total_ticks=200,
            avg_duration_ms=3.5,
            max_duration_ms=8.0,
            min_duration_ms=1.0,
            overrun_count=0,
            overrun_pct=0.0,
        )
        percentiles = PercentileStats(
            p50_ms=3.5, p95_ms=6.0, p99_ms=8.0, jitter_ms=1.0
        )
        scenarios = [
            ScenarioResult(
                client_count=count,
                tick_health=tick,
                percentiles=percentiles,
                throughput_actions_per_sec=100.0,
                passed=True,
                message="All thresholds met",
            )
            for count in [0, 10, 50]
        ]
        passed, message = evaluate_benchmark(scenarios)
        assert passed is True
        assert "50" in message


class TestOverallEvalPartialFail:
    """evaluate_benchmark fails and identifies failing count."""

    def test_partial_fail(self) -> None:
        from wowsim.benchmark import evaluate_benchmark
        from wowsim.models import PercentileStats, ScenarioResult, TickHealth

        tick = TickHealth(
            total_ticks=200,
            avg_duration_ms=3.5,
            max_duration_ms=8.0,
            min_duration_ms=1.0,
            overrun_count=0,
            overrun_pct=0.0,
        )
        percentiles = PercentileStats(
            p50_ms=3.5, p95_ms=6.0, p99_ms=8.0, jitter_ms=1.0
        )
        scenarios = [
            ScenarioResult(
                client_count=0,
                tick_health=tick,
                percentiles=percentiles,
                throughput_actions_per_sec=0.0,
                passed=True,
                message="OK",
            ),
            ScenarioResult(
                client_count=50,
                tick_health=tick,
                percentiles=percentiles,
                throughput_actions_per_sec=100.0,
                passed=True,
                message="OK",
            ),
            ScenarioResult(
                client_count=100,
                tick_health=tick,
                percentiles=percentiles,
                throughput_actions_per_sec=200.0,
                passed=False,
                message="p99 exceeded",
            ),
        ]
        passed, message = evaluate_benchmark(scenarios)
        assert passed is False
        assert "100" in message


# ============================================================
# Group F: Formatting (2 tests)
# ============================================================


def _make_passing_scenario(count: int = 50) -> "ScenarioResult":
    """Helper: a passing ScenarioResult."""
    from wowsim.models import PercentileStats, ScenarioResult, TickHealth

    tick = TickHealth(
        total_ticks=200,
        avg_duration_ms=3.5,
        max_duration_ms=8.0,
        min_duration_ms=1.0,
        overrun_count=0,
        overrun_pct=0.0,
    )
    percentiles = PercentileStats(
        p50_ms=3.5, p95_ms=6.0, p99_ms=8.2, jitter_ms=1.0
    )
    return ScenarioResult(
        client_count=count,
        tick_health=tick,
        percentiles=percentiles,
        throughput_actions_per_sec=100.0,
        passed=True,
        message="All thresholds met",
    )


class TestFormatScenarioResult:
    """format_scenario_result produces a one-line summary."""

    def test_format_pass(self) -> None:
        from wowsim.benchmark import format_scenario_result

        scenario = _make_passing_scenario(50)
        text = format_scenario_result(scenario)
        assert "PASS" in text
        assert "50" in text
        assert "3.5" in text
        assert "8.2" in text


class TestFormatBenchmarkResult:
    """format_benchmark_result produces a multi-line report."""

    def test_format(self) -> None:
        from wowsim.benchmark import format_benchmark_result
        from wowsim.models import BenchmarkConfig, BenchmarkResult

        config = BenchmarkConfig(client_counts=[0, 50])
        result = BenchmarkResult(
            config=config,
            scenarios=[_make_passing_scenario(0), _make_passing_scenario(50)],
            overall_passed=True,
            summary_message="All scenarios passed (max 50 clients)",
            total_duration_seconds=25.0,
        )
        text = format_benchmark_result(result)
        assert "Benchmark" in text
        assert "PASS" in text
        assert "50" in text
        assert "25.0" in text or "25.00" in text


# ============================================================
# Group G: Orchestration with monkeypatch (3 tests)
# ============================================================


def _make_tick_entries_for_health(
    count: int = 200, avg_ms: float = 3.5
) -> list:
    """Build tick entries for orchestrator tests."""
    return _make_tick_entries([avg_ms] * count)


class TestOrchestratorHappyPath:
    """run_benchmark passes all scenarios when server is healthy."""

    def test_happy_path(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from wowsim import benchmark
        from wowsim.models import BenchmarkConfig, ClientConfig, SpawnResult

        entries = _make_tick_entries_for_health(200, 3.5)
        spawn = SpawnResult(
            total_clients=10,
            successful_connections=10,
            failed_connections=0,
            total_actions_sent=100,
            total_duration_seconds=10.0,
            clients=[],
        )

        monkeypatch.setattr(benchmark, "_spawn_clients", lambda _cfg, _n: spawn)
        monkeypatch.setattr(benchmark, "_read_telemetry", lambda _cfg: entries)
        monkeypatch.setattr(benchmark, "_settle", lambda _s: None)

        config = BenchmarkConfig(
            client_counts=[0, 10],
            duration_seconds=1.0,
            settle_seconds=0.0,
        )
        result = benchmark.run_benchmark(config)
        assert result.overall_passed is True
        assert len(result.scenarios) == 2


class TestOrchestratorFailAt100:
    """run_benchmark fails when 100-client scenario exceeds thresholds."""

    def test_fail_at_100(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from wowsim import benchmark
        from wowsim.models import BenchmarkConfig, SpawnResult

        good_entries = _make_tick_entries_for_health(200, 3.5)
        bad_entries = _make_tick_entries_for_health(200, 60.0)
        spawn = SpawnResult(
            total_clients=10,
            successful_connections=10,
            failed_connections=0,
            total_actions_sent=100,
            total_duration_seconds=10.0,
            clients=[],
        )

        call_count = {"n": 0}

        def mock_read(_cfg):
            call_count["n"] += 1
            if call_count["n"] <= 2:
                return good_entries
            return bad_entries

        monkeypatch.setattr(benchmark, "_spawn_clients", lambda _cfg, _n: spawn)
        monkeypatch.setattr(benchmark, "_read_telemetry", mock_read)
        monkeypatch.setattr(benchmark, "_settle", lambda _s: None)

        config = BenchmarkConfig(
            client_counts=[0, 10, 100],
            duration_seconds=1.0,
            settle_seconds=0.0,
        )
        result = benchmark.run_benchmark(config)
        assert result.overall_passed is False
        assert any(not s.passed for s in result.scenarios)


class TestOrchestratorBaselineSkipsSpawn:
    """run_benchmark skips client spawn for count=0 (baseline)."""

    def test_baseline(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from wowsim import benchmark
        from wowsim.models import BenchmarkConfig

        entries = _make_tick_entries_for_health(200, 3.5)
        spawn_called = {"count": 0}

        def mock_spawn(_cfg, _n):
            spawn_called["count"] += 1

        monkeypatch.setattr(benchmark, "_spawn_clients", mock_spawn)
        monkeypatch.setattr(benchmark, "_read_telemetry", lambda _cfg: entries)
        monkeypatch.setattr(benchmark, "_settle", lambda _s: None)

        config = BenchmarkConfig(
            client_counts=[0],
            duration_seconds=1.0,
            settle_seconds=0.0,
        )
        result = benchmark.run_benchmark(config)
        assert spawn_called["count"] == 0
        assert len(result.scenarios) == 1
