"""Health check reporter for the WoW server simulator.

Aggregates server health from telemetry logs and presents periodic
health summaries including tick rate stability, zone health, player
counts, and anomaly detection.
"""

from __future__ import annotations

from wowsim.models import TelemetryEntry, TickHealth


def compute_tick_health(entries: list[TelemetryEntry]) -> TickHealth | None:
    """Extract tick rate stats from game_loop 'Tick completed' metrics.

    Returns None if no tick metrics are found in the entries.
    """
    tick_entries = [
        e
        for e in entries
        if e.type == "metric"
        and e.component == "game_loop"
        and e.message == "Tick completed"
    ]
    if not tick_entries:
        return None

    durations = [e.data.get("duration_ms", 0.0) for e in tick_entries]
    overruns = sum(1 for e in tick_entries if e.data.get("overrun", False))
    total = len(tick_entries)

    return TickHealth(
        total_ticks=total,
        avg_duration_ms=sum(durations) / total,
        max_duration_ms=max(durations),
        min_duration_ms=min(durations),
        overrun_count=overruns,
        overrun_pct=(overruns / total) * 100.0,
    )
