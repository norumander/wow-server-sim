"""Health check reporter for the WoW server simulator.

Aggregates server health from telemetry logs and presents periodic
health summaries including tick rate stability, zone health, player
counts, and anomaly detection.
"""

from __future__ import annotations

import socket
from collections import defaultdict
from datetime import UTC, datetime
from pathlib import Path

from wowsim.log_parser import detect_anomalies, parse_line
from wowsim.models import (
    Anomaly,
    FaultInfo,
    GameMechanicSummary,
    HealthReport,
    TelemetryEntry,
    TickHealth,
    ZoneHealthSummary,
)

# ---------------------------------------------------------------------------
# Core computation (pure functions on TelemetryEntry lists)
# ---------------------------------------------------------------------------


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


def compute_zone_health(entries: list[TelemetryEntry]) -> list[ZoneHealthSummary]:
    """Extract per-zone health from zone tick metrics and zone errors."""
    zone_ticks: dict[int, list[float]] = defaultdict(list)
    zone_errors: dict[int, int] = defaultdict(int)

    for entry in entries:
        if (
            entry.type == "metric"
            and entry.component == "zone"
            and entry.message == "Zone tick completed"
        ):
            zone_id = entry.data.get("zone_id", 0)
            duration = entry.data.get("duration_ms", 0.0)
            zone_ticks[zone_id].append(duration)
        elif (
            entry.type == "error"
            and entry.component == "zone"
            and entry.message == "Zone tick exception"
        ):
            zone_id = entry.data.get("zone_id", 0)
            zone_errors[zone_id] += 1

    all_zone_ids = set(zone_ticks.keys()) | set(zone_errors.keys())
    summaries: list[ZoneHealthSummary] = []

    for zone_id in sorted(all_zone_ids):
        ticks = zone_ticks.get(zone_id, [])
        errors = zone_errors.get(zone_id, 0)

        if errors > 0:
            state = "CRASHED"
        else:
            state = "ACTIVE"

        avg_duration = sum(ticks) / len(ticks) if ticks else 0.0

        summaries.append(
            ZoneHealthSummary(
                zone_id=zone_id,
                state=state,
                tick_count=len(ticks),
                error_count=errors,
                avg_tick_duration_ms=avg_duration,
            )
        )

    return summaries


def estimate_player_count(entries: list[TelemetryEntry]) -> int:
    """Net player count = connections accepted - disconnections."""
    connections = 0
    disconnections = 0
    for entry in entries:
        if entry.type == "event" and entry.component == "game_server":
            if entry.message == "Connection accepted":
                connections += 1
            elif entry.message == "Client disconnected":
                disconnections += 1
    return max(0, connections - disconnections)


def determine_status(
    tick: TickHealth | None,
    zones: list[ZoneHealthSummary],
    anomalies: list[Anomaly],
    game_mechanics: GameMechanicSummary | None = None,
    connected_players: int = 0,
) -> str:
    """Determine overall health status: 'healthy', 'degraded', or 'critical'."""
    # Critical: any critical anomaly OR any CRASHED zone
    if any(a.severity == "critical" for a in anomalies):
        return "critical"
    if any(z.state == "CRASHED" for z in zones):
        return "critical"

    # Degraded: any warning anomaly OR any DEGRADED zone OR overrun_pct > 10%
    if any(a.severity == "warning" for a in anomalies):
        return "degraded"
    if any(z.state == "DEGRADED" for z in zones):
        return "degraded"
    if tick is not None and tick.overrun_pct > 10.0:
        return "degraded"

    # Degraded: game-mechanic signals
    if game_mechanics is not None:
        gm = game_mechanics
        if gm.cast_metrics.gcd_block_rate > 0.5:
            return "degraded"
        if gm.cast_metrics.casts_started > 0 and gm.cast_metrics.cast_success_rate < 0.5:
            return "degraded"
        if (
            connected_players > 0
            and gm.combat_metrics.total_attacks == 0
            and gm.cast_metrics.casts_started == 0
        ):
            return "degraded"

    return "healthy"


# ---------------------------------------------------------------------------
# I/O helpers
# ---------------------------------------------------------------------------


def check_server_reachable(host: str, port: int, timeout: float = 2.0) -> bool:
    """TCP connect test — returns True if server accepts connection."""
    try:
        conn = socket.create_connection((host, port), timeout=timeout)
        conn.close()
        return True
    except OSError:
        return False


def read_recent_entries(
    log_path: Path,
    max_lines: int = 500,
) -> list[TelemetryEntry]:
    """Read last max_lines from telemetry log file, parse valid entries."""
    with open(log_path) as f:
        all_lines = f.readlines()
    recent = all_lines[-max_lines:] if len(all_lines) > max_lines else all_lines
    entries: list[TelemetryEntry] = []
    for line in recent:
        entry = parse_line(line)
        if entry is not None:
            entries.append(entry)
    return entries


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------


def build_health_report(
    log_path: Path | None = None,
    game_host: str = "localhost",
    game_port: int = 8080,
    control_host: str = "localhost",
    control_port: int = 8081,
    skip_faults: bool = False,
) -> HealthReport:
    """Build a complete health report from log file + server check + fault query."""
    reachable = check_server_reachable(control_host, control_port)

    entries: list[TelemetryEntry] = []
    if log_path is not None:
        entries = read_recent_entries(log_path)

    tick = compute_tick_health(entries)
    zones = compute_zone_health(entries)
    players = estimate_player_count(entries)
    anomalies = detect_anomalies(entries)
    error_count = sum(1 for e in entries if e.type == "error")
    uptime_ticks = tick.total_ticks if tick else 0

    # Game-mechanic aggregation
    game_mechanics: GameMechanicSummary | None = None
    if entries:
        from wowsim.game_metrics import aggregate_game_mechanics

        game_mechanics = aggregate_game_mechanics(entries)

    active_faults: list[FaultInfo] = []
    if not skip_faults:
        try:
            from wowsim.fault_trigger import list_all_faults

            resp = list_all_faults(control_host, control_port)
            if resp.faults:
                active_faults = [f for f in resp.faults if f.active]
        except (OSError, Exception):
            pass

    status = determine_status(
        tick, zones, anomalies,
        game_mechanics=game_mechanics,
        connected_players=players,
    )

    return HealthReport(
        timestamp=datetime.now(UTC),
        status=status,
        server_reachable=reachable,
        tick=tick,
        zones=zones,
        connected_players=players,
        anomalies=anomalies,
        active_faults=active_faults,
        error_count=error_count,
        uptime_ticks=uptime_ticks,
        game_mechanics=game_mechanics,
    )


# ---------------------------------------------------------------------------
# Formatting
# ---------------------------------------------------------------------------


def format_health_report(report: HealthReport) -> str:
    """Human-readable multi-line health report."""
    lines: list[str] = []
    lines.append("=== WoW Server Health Report ===")
    lines.append(f"Timestamp: {report.timestamp.isoformat()}")
    lines.append(f"Status:    {report.status.upper()}")
    lines.append("")

    reachable_str = "reachable" if report.server_reachable else "unreachable"
    lines.append(f"Server: {reachable_str}")
    lines.append("")

    if report.tick:
        t = report.tick
        lines.append("Tick Rate:")
        lines.append(f"  Total ticks:    {t.total_ticks}")
        lines.append(f"  Avg duration:   {t.avg_duration_ms:.1f}ms")
        lines.append(
            f"  Max:            {t.max_duration_ms:.1f}ms"
            f"  Min: {t.min_duration_ms:.1f}ms"
        )
        lines.append(f"  Overruns:       {t.overrun_count} ({t.overrun_pct:.1f}%)")
        lines.append("")

    if report.zones:
        lines.append("Zones:")
        for z in report.zones:
            lines.append(
                f"  Zone {z.zone_id:<3}  {z.state:<10}  "
                f"{z.tick_count} ticks   {z.error_count} errors   "
                f"avg {z.avg_tick_duration_ms:.1f}ms"
            )
        lines.append("")

    if report.game_mechanics:
        gm = report.game_mechanics
        c = gm.cast_metrics
        cm = gm.combat_metrics
        lines.append("Game Mechanics:")
        lines.append(f"  Cast success:   {c.cast_success_rate * 100:.1f}%"
                      f"  ({c.casts_completed}/{c.casts_started} casts)")
        lines.append(f"  GCD block rate: {c.gcd_block_rate * 100:.1f}%")
        lines.append(f"  Overall DPS:    {cm.overall_dps:.1f}"
                      f"  ({cm.total_damage} dmg / {cm.total_attacks} attacks)")
        lines.append(f"  Kills:          {cm.kills}")
        lines.append("")

    lines.append(f"Connected Players: {report.connected_players}")

    if report.active_faults:
        fault_strs = [f.id for f in report.active_faults]
        lines.append(f"Active Faults: {', '.join(fault_strs)}")
    else:
        lines.append("Active Faults: none")

    if report.anomalies:
        lines.append(f"Anomalies: {len(report.anomalies)}")
        for a in report.anomalies:
            severity_tag = "CRITICAL" if a.severity == "critical" else "WARNING"
            lines.append(f"  [{severity_tag}] {a.type}: {a.message}")
    else:
        lines.append("Anomalies: none")

    return "\n".join(lines)
