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


def compute_suggestion(
    players: int, active_faults: int, status: str, pipeline_ran: bool
) -> str:
    """Return context-aware guidance text for the suggestion bar.

    Driven by current dashboard state: player count, active faults,
    health status, and whether the pipeline has run this session.
    """
    if players == 0:
        return "Press [bold]s[/] to spawn players"
    if active_faults > 0 and status == "critical":
        return "Press [bold]d[/] to deactivate fault and recover"
    if active_faults > 0:
        return "Fault active — observe metrics"
    if pipeline_ran:
        return "Press [bold]s[/] for another scenario, or [bold]q[/] to quit"
    return "Press [bold]a[/] to inject a fault, or [bold]p[/] to run pipeline"


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


# ---------------------------------------------------------------------------
# Textual TUI Application
# ---------------------------------------------------------------------------


def _format_fault_table(faults: list) -> str:
    """Format fault list as a text table for the fault panel."""
    if not faults:
        return "No faults registered"
    header = f"{'ID':<24s} {'MODE':<14s} {'STATUS':<10s} {'ACTIVATIONS'}"
    lines = [header, "-" * len(header)]
    for f in faults:
        status_str = "[bold]ACTIVE[/]" if f.active else "inactive"
        lines.append(
            f"{f.id:<24s} {f.mode:<14s} {status_str:<10s} {f.activations}"
        )
    return "\n".join(lines)


try:
    from textual import work
    from textual.app import App, ComposeResult
    from textual.containers import Horizontal
    from textual.widgets import DataTable, Footer, Header, RichLog, Static

    class WoWDashboardApp(App):
        """Textual TUI dashboard for the WoW Server Simulator."""

        CSS_PATH = "dashboard.tcss"
        TITLE = "WoW Server Simulator — Dashboard"

        BINDINGS = [
            ("q", "quit", "Quit"),
            ("r", "refresh", "Refresh"),
            ("s", "spawn_clients", "Spawn"),
            ("a", "activate_fault", "Activate"),
            ("d", "deactivate_fault", "Deactivate"),
            ("x", "deactivate_all", "Deact All"),
        ]

        def __init__(self, config: DashboardConfig) -> None:
            super().__init__()
            self._config = config
            self._last_entry_ts: datetime | None = None
            self._fault_list: list = []
            self._player_count: int = 0
            self._health_status: str = "healthy"
            self._pipeline_ran: bool = False

        def compose(self) -> ComposeResult:
            """Build the widget tree."""
            yield Header()
            yield Static("Loading...", id="status-bar")
            with Horizontal(id="panels"):
                yield Static("TICK METRICS\n\nLoading...", id="tick-panel")
                yield DataTable(id="zone-table")
            yield Static("FAULT CONTROL\n\nLoading...", id="fault-panel")
            yield RichLog(id="event-log", highlight=True, markup=True)
            yield Static("Loading...", id="suggestion-bar")
            yield Footer()

        def on_mount(self) -> None:
            """Initialize zone table columns and start refresh timer."""
            table = self.query_one("#zone-table", DataTable)
            table.add_columns("Zone", "State", "Ticks", "Errors", "Avg (ms)")
            self.set_interval(self._config.refresh_interval, self._trigger_refresh)
            self._trigger_refresh()

        def _trigger_refresh(self) -> None:
            """Kick off both health and fault data fetches."""
            self._fetch_health_data()
            self._fetch_fault_list()

        @work(exclusive=True, thread=True)
        def _fetch_health_data(self) -> None:
            """Fetch health data in a worker thread (sync I/O)."""
            from wowsim.health_check import (
                check_server_reachable,
                compute_tick_health,
                compute_zone_health,
                determine_status,
                estimate_player_count,
                read_recent_entries,
            )
            from wowsim.log_parser import detect_anomalies

            try:
                entries = read_recent_entries(self._config.log_file)
            except OSError:
                entries = []

            tick = compute_tick_health(entries)
            zones = compute_zone_health(entries)
            players = estimate_player_count(entries)
            anomalies = detect_anomalies(entries)
            status = determine_status(tick, zones, anomalies)
            reachable = check_server_reachable(
                self._config.host, self._config.control_port, timeout=1.0
            )
            uptime = tick.total_ticks if tick else 0

            new_entries, new_ts = filter_new_entries(entries, self._last_entry_ts)

            self.call_from_thread(
                self._update_health_ui,
                status, reachable, players, uptime, tick, zones, new_entries, new_ts,
            )

        def _update_health_ui(
            self,
            status: str,
            reachable: bool,
            players: int,
            uptime: int,
            tick: TickHealth | None,
            zones: list,
            new_entries: list[TelemetryEntry],
            new_ts: datetime | None,
        ) -> None:
            """Update UI widgets with health data (runs on main thread)."""
            # Status bar
            status_bar = self.query_one("#status-bar", Static)
            status_bar.update(format_status_bar(status, reachable, players, uptime))

            # Tick panel
            tick_panel = self.query_one("#tick-panel", Static)
            tick_panel.update(f"TICK METRICS\n\n{format_tick_panel(tick)}")

            # Zone table
            table = self.query_one("#zone-table", DataTable)
            table.clear()
            for z in zones:
                table.add_row(
                    str(z.zone_id),
                    z.state,
                    str(z.tick_count),
                    str(z.error_count),
                    f"{z.avg_tick_duration_ms:.1f}",
                )

            # Event log
            if new_entries:
                self._last_entry_ts = new_ts
                log = self.query_one("#event-log", RichLog)
                for entry in new_entries:
                    log.write(format_event_line(entry))

            # Track state for suggestion bar
            self._player_count = players
            self._health_status = status
            self._update_suggestion()

        def _update_suggestion(self) -> None:
            """Update the suggestion bar based on current state."""
            active_faults = sum(1 for f in self._fault_list if f.active)
            text = compute_suggestion(
                self._player_count,
                active_faults,
                self._health_status,
                self._pipeline_ran,
            )
            bar = self.query_one("#suggestion-bar", Static)
            bar.update(text)

        @work(exclusive=True)
        async def _fetch_fault_list(self) -> None:
            """Fetch fault list via async ControlClient."""
            from wowsim.fault_trigger import ControlClient

            try:
                async with ControlClient(
                    self._config.host, self._config.control_port
                ) as client:
                    resp = await client.list_faults()
                    faults = resp.faults or []
            except (OSError, Exception):
                faults = self._fault_list

            self._fault_list = faults
            fault_panel = self.query_one("#fault-panel", Static)
            fault_panel.update(
                f"FAULT CONTROL\n\n{_format_fault_table(faults)}"
            )

        def action_refresh(self) -> None:
            """Manual refresh triggered by 'r' key."""
            self._trigger_refresh()
            self.notify("Refreshed")

        def action_spawn_clients(self) -> None:
            """Spawn 5 mock clients via thread worker (key: s)."""
            self.notify("Spawning 5 clients...")
            self._do_spawn_clients()

        @work(thread=True)
        def _do_spawn_clients(self) -> None:
            """Run mock client spawn in a worker thread."""
            from wowsim.mock_client import run_spawn
            from wowsim.models import ClientConfig

            config = ClientConfig(
                host=self._config.host,
                port=self._config.port,
            )
            result = run_spawn(config, 5)
            ok = result.successful_connections
            self.call_from_thread(
                self.notify,
                f"Spawned {ok}/5 clients",
            )
            self.call_from_thread(self._trigger_refresh)

        async def action_activate_fault(self) -> None:
            """Activate the first inactive fault (demo convenience)."""
            from wowsim.fault_trigger import ControlClient

            target = None
            for f in self._fault_list:
                if not f.active:
                    target = f
                    break
            if target is None:
                self.notify("No inactive faults", severity="warning")
                return

            try:
                async with ControlClient(
                    self._config.host, self._config.control_port
                ) as client:
                    await client.activate(target.id)
                self.notify(f"Activated {target.id}")
            except (OSError, Exception) as exc:
                self.notify(f"Error: {exc}", severity="error")

            self._trigger_refresh()

        async def action_deactivate_fault(self) -> None:
            """Deactivate the first active fault (demo convenience)."""
            from wowsim.fault_trigger import ControlClient

            target = None
            for f in self._fault_list:
                if f.active:
                    target = f
                    break
            if target is None:
                self.notify("No active faults", severity="warning")
                return

            try:
                async with ControlClient(
                    self._config.host, self._config.control_port
                ) as client:
                    await client.deactivate(target.id)
                self.notify(f"Deactivated {target.id}")
            except (OSError, Exception) as exc:
                self.notify(f"Error: {exc}", severity="error")

            self._trigger_refresh()

        async def action_deactivate_all(self) -> None:
            """Deactivate all active faults."""
            from wowsim.fault_trigger import ControlClient

            try:
                async with ControlClient(
                    self._config.host, self._config.control_port
                ) as client:
                    await client.deactivate_all()
                self.notify("All faults deactivated")
            except (OSError, Exception) as exc:
                self.notify(f"Error: {exc}", severity="error")

            self._trigger_refresh()

except ImportError:
    pass
