"""Monitoring dashboard for the WoW Server Simulator.

Pure formatting functions (top of file) are independently testable
without any Textual dependency. The TUI app class follows below.
"""

from __future__ import annotations

from datetime import datetime
from pathlib import Path

from pydantic import BaseModel

from wowsim.models import TelemetryEntry, TickHealth


# ---------------------------------------------------------------------------
# Pure formatting functions (no UI dependency)
# ---------------------------------------------------------------------------


def format_status_bar(
    status: str, reachable: bool, players: int, uptime: int
) -> str:
    """Format the status bar line with Rich markup.

    Returns a single-line string showing overall status, server
    reachability, player count, and uptime ticks.
    """
    status_upper = status.upper()
    reachable_str = "reachable" if reachable else "unreachable"
    return (
        f"[{status_to_style(status)}]{status_upper}[/]  "
        f"Server: {reachable_str}  |  "
        f"Players: {players}  |  "
        f"Uptime: {uptime:,} ticks"
    )


def format_tick_panel(tick: TickHealth | None) -> str:
    """Format tick metrics as a multi-line panel string.

    Returns 'No data' placeholder when tick health is None.
    """
    if tick is None:
        return "No data"
    return (
        f"Avg: {tick.avg_duration_ms:.1f}ms\n"
        f"Max: {tick.max_duration_ms:.1f}ms  "
        f"Min: {tick.min_duration_ms:.1f}ms\n"
        f"Overruns: {tick.overrun_count} ({tick.overrun_pct:.1f}%)"
    )


def format_event_line(entry: TelemetryEntry) -> str:
    """Format a telemetry entry as a compact single-line log string.

    Example: '[19:17:10] metric  zone           Zone tick completed'
    """
    ts = entry.timestamp.strftime("%H:%M:%S")
    return f"[{ts}] {entry.type:<7s} {entry.component:<14s} {entry.message}"


def status_to_style(status: str) -> str:
    """Map a health status string to a Rich style string."""
    return {
        "healthy": "bold green",
        "degraded": "bold yellow",
        "critical": "bold red",
    }.get(status, "bold white")


def fault_action_label(active: bool) -> str:
    """Return the appropriate action label for a fault's current state."""
    return "Deactivate" if active else "Activate"


def filter_new_entries(
    entries: list[TelemetryEntry],
    last_ts: datetime | None,
) -> tuple[list[TelemetryEntry], datetime | None]:
    """Filter entries using a timestamp watermark to avoid duplicates.

    On first load (last_ts is None), returns the last 20 entries.
    On subsequent loads, returns only entries newer than last_ts.
    Returns (filtered_entries, new_watermark).
    """
    if not entries:
        return [], None

    if last_ts is None:
        result = entries[-20:]
    else:
        result = [e for e in entries if e.timestamp > last_ts]

    if result:
        new_ts = result[-1].timestamp
    else:
        new_ts = last_ts

    return result, new_ts


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------


class DashboardConfig(BaseModel):
    """Configuration for the monitoring dashboard."""

    log_file: Path
    host: str = "localhost"
    port: int = 8080
    control_port: int = 8081
    refresh_interval: float = 2.0
