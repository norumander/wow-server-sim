"""Performance benchmark suite for the WoW server simulator.

Orchestrates scaling tests by composing mock_client, health_check,
and percentile computation into automated benchmark scenarios with
pass/fail evaluation against configurable thresholds.
"""

from __future__ import annotations

import math

from wowsim.models import PercentileStats, TelemetryEntry


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
