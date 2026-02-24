"""Performance benchmark suite for the WoW server simulator.

Orchestrates scaling tests by composing mock_client, health_check,
and percentile computation into automated benchmark scenarios with
pass/fail evaluation against configurable thresholds.
"""

from __future__ import annotations

import math
import time

from wowsim.models import (
    BenchmarkConfig,
    BenchmarkResult,
    PercentileStats,
    ScenarioResult,
    SpawnResult,
    TelemetryEntry,
    TickHealth,
)


# ---------------------------------------------------------------------------
# Pure functions (no I/O)
# ---------------------------------------------------------------------------


def compute_percentiles(entries: list[TelemetryEntry]) -> PercentileStats | None:
    """Compute P50/P95/P99 and jitter from tick duration metrics.

    Uses nearest-rank percentile method. Returns None if no tick
    metrics are found in the entries.
    """
    durations = [
        e.data.get("duration_ms", 0.0)
        for e in entries
        if e.type == "metric"
        and e.component == "game_loop"
        and e.message == "Tick completed"
    ]
    if not durations:
        return None

    durations.sort()
    n = len(durations)

    def _percentile(pct: float) -> float:
        """Nearest-rank percentile."""
        rank = math.ceil(pct / 100.0 * n)
        return durations[min(rank, n) - 1]

    mean = sum(durations) / n
    variance = sum((d - mean) ** 2 for d in durations) / n
    jitter = math.sqrt(variance)

    return PercentileStats(
        p50_ms=_percentile(50),
        p95_ms=_percentile(95),
        p99_ms=_percentile(99),
        jitter_ms=round(jitter, 6),
    )


def compute_throughput(spawn_result: SpawnResult) -> float:
    """Compute actions per second from a spawn result.

    Returns 0.0 if duration is zero to avoid division by zero.
    """
    if spawn_result.total_duration_seconds <= 0.0:
        return 0.0
    return spawn_result.total_actions_sent / spawn_result.total_duration_seconds


def evaluate_scenario(
    tick_health: TickHealth,
    percentiles: PercentileStats,
    throughput: float,
    client_count: int,
    config: BenchmarkConfig,
) -> ScenarioResult:
    """Evaluate a single benchmark scenario against thresholds.

    Passes when avg <= max_avg, p99 <= max_p99, and overrun_pct <= max_overrun.
    """
    failures: list[str] = []

    if tick_health.avg_duration_ms > config.max_avg_tick_ms:
        failures.append(
            f"avg {tick_health.avg_duration_ms:.1f}ms > {config.max_avg_tick_ms:.1f}ms"
        )
    if percentiles.p99_ms > config.max_p99_tick_ms:
        failures.append(
            f"p99 {percentiles.p99_ms:.1f}ms > {config.max_p99_tick_ms:.1f}ms"
        )
    if tick_health.overrun_pct > config.max_overrun_pct:
        failures.append(
            f"overrun {tick_health.overrun_pct:.1f}% > {config.max_overrun_pct:.1f}%"
        )

    passed = len(failures) == 0
    message = "All thresholds met" if passed else "; ".join(failures)

    return ScenarioResult(
        client_count=client_count,
        tick_health=tick_health,
        percentiles=percentiles,
        throughput_actions_per_sec=throughput,
        passed=passed,
        message=message,
    )


def evaluate_benchmark(
    scenarios: list[ScenarioResult],
) -> tuple[bool, str]:
    """Evaluate overall benchmark result from scenario results.

    All scenarios must pass. Summary identifies max passing client count
    or the first failing count.
    """
    all_passed = all(s.passed for s in scenarios)
    passing = [s for s in scenarios if s.passed]
    failing = [s for s in scenarios if not s.passed]

    if all_passed:
        max_count = max(s.client_count for s in passing) if passing else 0
        return (True, f"All scenarios passed (max {max_count} clients)")

    first_fail = failing[0]
    max_passing = max(s.client_count for s in passing) if passing else 0
    return (
        False,
        f"Failed at {first_fail.client_count} clients "
        f"(max passing: {max_passing})",
    )


# ---------------------------------------------------------------------------
# Formatting (no I/O)
# ---------------------------------------------------------------------------


def format_scenario_result(result: ScenarioResult) -> str:
    """One-line scenario summary: [PASS/FAIL] N clients — avg, p99, overrun%."""
    tag = "PASS" if result.passed else "FAIL"
    return (
        f"[{tag}] {result.client_count} clients — "
        f"avg {result.tick_health.avg_duration_ms:.1f}ms, "
        f"p99 {result.percentiles.p99_ms:.1f}ms, "
        f"{result.tick_health.overrun_pct:.1f}% overrun"
    )


def format_benchmark_result(result: BenchmarkResult) -> str:
    """Multi-line benchmark report with header, per-scenario lines, and outcome."""
    lines: list[str] = []
    lines.append("=== Benchmark Report ===")
    lines.append(
        f"Clients: {result.config.client_counts}  "
        f"Duration: {result.config.duration_seconds}s/scenario"
    )
    lines.append("")
    lines.append("Scenarios:")
    for scenario in result.scenarios:
        lines.append(f"  {format_scenario_result(scenario)}")
    lines.append("")
    tag = "PASS" if result.overall_passed else "FAIL"
    lines.append(f"Result: [{tag}] {result.summary_message}")
    lines.append(f"Total:  {result.total_duration_seconds:.2f}s")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# I/O wrappers (thin, mockable via monkeypatch)
# ---------------------------------------------------------------------------


def _spawn_clients(config: BenchmarkConfig, count: int) -> SpawnResult:
    """Spawn N mock clients using the benchmark config's settings."""
    from wowsim.mock_client import run_spawn
    from wowsim.models import ClientConfig

    client_config = ClientConfig(
        host=config.game_host,
        port=config.game_port,
        actions_per_second=config.actions_per_second,
        duration_seconds=config.duration_seconds,
    )
    return run_spawn(client_config, count)


def _read_telemetry(config: BenchmarkConfig) -> list[TelemetryEntry]:
    """Read recent telemetry entries from the log file."""
    from pathlib import Path

    from wowsim.health_check import read_recent_entries

    if config.log_file is None:
        return []
    return read_recent_entries(Path(config.log_file), max_lines=2000)


def _settle(seconds: float) -> None:
    """Wait between scenarios for the server to stabilize."""
    time.sleep(seconds)


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------


def run_benchmark(config: BenchmarkConfig) -> BenchmarkResult:
    """Run the full benchmark suite across all configured client counts.

    For each client_count:
    1. Spawn clients (skipped for count=0 baseline)
    2. Settle (wait for server to stabilize)
    3. Read telemetry and compute tick health + percentiles
    4. Evaluate against thresholds
    """
    from wowsim.health_check import compute_tick_health

    start = time.monotonic()
    scenarios: list[ScenarioResult] = []

    for count in config.client_counts:
        throughput = 0.0

        if count > 0:
            spawn_result = _spawn_clients(config, count)
            throughput = compute_throughput(spawn_result)

        _settle(config.settle_seconds)

        entries = _read_telemetry(config)
        tick_health = compute_tick_health(entries)
        percentiles = compute_percentiles(entries)

        if tick_health is None:
            tick_health = TickHealth(
                total_ticks=0,
                avg_duration_ms=0.0,
                max_duration_ms=0.0,
                min_duration_ms=0.0,
                overrun_count=0,
                overrun_pct=0.0,
            )
        if percentiles is None:
            percentiles = PercentileStats(
                p50_ms=0.0, p95_ms=0.0, p99_ms=0.0, jitter_ms=0.0
            )

        scenario = evaluate_scenario(
            tick_health, percentiles, throughput, count, config
        )
        scenarios.append(scenario)

    overall_passed, summary = evaluate_benchmark(scenarios)

    return BenchmarkResult(
        config=config,
        scenarios=scenarios,
        overall_passed=overall_passed,
        summary_message=summary,
        total_duration_seconds=time.monotonic() - start,
    )
