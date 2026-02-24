"""Performance benchmark suite for the WoW server simulator.

Orchestrates scaling tests by composing mock_client, health_check,
and percentile computation into automated benchmark scenarios with
pass/fail evaluation against configurable thresholds.
"""

from __future__ import annotations

import math

from wowsim.models import (
    BenchmarkConfig,
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
