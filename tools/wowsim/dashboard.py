"""Monitoring dashboard for the WoW Server Simulator.

Pure formatting functions (top of file) are independently testable
without any Textual dependency. The TUI app class follows below.
"""

from __future__ import annotations

from datetime import datetime
from pathlib import Path

from pydantic import BaseModel

from wowsim.models import EntityDPS, GameMechanicSummary, TelemetryEntry, TickHealth


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

ZONE_COLUMNS: tuple[str, ...] = ("Zone", "State", "Ticks", "Errors", "Avg (ms)", "Casts", "DPS")
"""Column headers for the zone health DataTable (7 columns)."""


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


def format_game_mechanics_panel(summary: GameMechanicSummary | None) -> str:
    """Format game mechanics as a multi-line panel string.

    Returns 'No data' placeholder when summary is None.
    """
    if summary is None:
        return "No data"
    c = summary.cast_metrics
    cm = summary.combat_metrics
    return (
        f"Cast: {c.cast_success_rate * 100:.1f}% success"
        f"  ({c.casts_completed}/{c.casts_started})\n"
        f"GCD block: {c.gcd_block_rate * 100:.1f}%"
        f"  Rate: {c.cast_rate_per_sec:.1f}/s\n"
        f"DPS: {cm.overall_dps:.1f}"
        f"  Dmg: {cm.total_damage}"
        f"  Kills: {cm.kills}"
    )


def format_threat_table_panel(top_dealers: list[EntityDPS] | None) -> str:
    """Format the threat table as a ranked list for the game mechanics panel.

    Returns 'No data' when top_dealers is None or empty.
    Damage ranking equals threat ranking per ADR-012.
    """
    if not top_dealers:
        return "No data"
    lines: list[str] = []
    for rank, dealer in enumerate(top_dealers, 1):
        lines.append(
            f"#{rank} Entity {dealer.entity_id}"
            f"      {dealer.total_damage} dmg"
            f"  {dealer.dps:.1f} DPS"
            f"  ({dealer.attack_count} atk)"
        )
    return "\n".join(lines)


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
    players: int,
    active_faults: int,
    status: str,
    pipeline_ran: bool,
    spawn_active: bool = False,
) -> str:
    """Return context-aware guidance text for the suggestion bar.

    Driven by current dashboard state: player count, active faults,
    health status, whether the pipeline has run, and whether a
    spawn batch is currently active.
    """
    if spawn_active:
        return (
            "Clients active — press [bold]k[/] to despawn, "
            "or [bold]a[/] to inject a fault"
        )
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


# ---------------------------------------------------------------------------
# Fault catalog (all 8 scenarios with default params)
# ---------------------------------------------------------------------------

FAULT_CATALOG: dict[str, dict] = {
    "latency-spike": {
        "description": "Add 200ms delay to tick processing",
        "params": {"delay_ms": 200},
    },
    "session-crash": {
        "description": "Force-terminate a random player session",
        "params": {},
    },
    "event-queue-flood": {
        "description": "Inject 10x normal event volume",
        "params": {"multiplier": 10},
    },
    "memory-pressure": {
        "description": "Allocate and hold large buffers (50 MB)",
        "params": {"megabytes": 50},
    },
    "cascading-zone-failure": {
        "description": "Crash zone 1, flood zone 2 with redistributed load",
        "params": {"source_zone": 1, "target_zone": 2},
    },
    "slow-leak": {
        "description": "Increment tick delay by 1ms every 100 ticks",
        "params": {"increment_ms": 1, "increment_every": 100},
    },
    "split-brain": {
        "description": "Partition two zones so they can't sync state",
        "params": {"phantom_count": 5},
    },
    "thundering-herd": {
        "description": "Disconnect all players, reconnect simultaneously",
        "params": {"reconnect_delay_ticks": 40},
    },
}
"""All 8 fault injection scenarios with descriptions and default params."""


# ---------------------------------------------------------------------------
# Duration options for spawn duration picker
# ---------------------------------------------------------------------------

DURATION_OPTIONS: list[tuple[str, float]] = [
    ("10 seconds", 10.0),
    ("30 seconds", 30.0),
    ("60 seconds", 60.0),
    ("Persistent (until stopped)", float("inf")),
]
"""Spawn duration choices: label and duration_seconds value."""


def format_fault_option(fault_id: str, description: str) -> str:
    """Format a fault as a single option line for the picker.

    Example: 'latency-spike — Add 200ms delay to tick processing'
    """
    return f"{fault_id} — {description}"


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
    import threading

    from textual import work
    from textual.app import App, ComposeResult
    from textual.containers import Horizontal
    from textual.screen import ModalScreen
    from textual.widgets import DataTable, Footer, Header, OptionList, RichLog, Static

    class DurationPickerScreen(ModalScreen[float | None]):
        """Modal for selecting a spawn duration."""

        BINDINGS = [("escape", "cancel", "Cancel")]

        def compose(self) -> ComposeResult:
            """Build an OptionList of duration choices."""
            options = OptionList(id="duration-options")
            for label, _ in DURATION_OPTIONS:
                options.add_option(label)
            yield options

        def on_option_list_option_selected(
            self, event: OptionList.OptionSelected
        ) -> None:
            """Dismiss with the selected duration value."""
            idx = event.option_index
            _, duration = DURATION_OPTIONS[idx]
            self.dismiss(duration)

        def action_cancel(self) -> None:
            """Dismiss without selection."""
            self.dismiss(None)

    class FaultPickerScreen(ModalScreen[str | None]):
        """Modal for selecting a fault to inject."""

        BINDINGS = [("escape", "cancel", "Cancel")]

        def compose(self) -> ComposeResult:
            """Build a simple OptionList of faults."""
            options = OptionList(id="fault-options")
            for fault_id, info in FAULT_CATALOG.items():
                options.add_option(format_fault_option(fault_id, info["description"]))
            yield options

        def on_option_list_option_selected(
            self, event: OptionList.OptionSelected
        ) -> None:
            """Dismiss with the selected fault ID."""
            # Extract fault_id from "fault-id — description"
            text = str(event.option.prompt)
            fault_id = text.split(" — ")[0]
            self.dismiss(fault_id)

        def action_cancel(self) -> None:
            """Dismiss without selection."""
            self.dismiss(None)

    class WoWDashboardApp(App):
        """Textual TUI dashboard for the WoW Server Simulator."""

        CSS_PATH = "dashboard.tcss"
        TITLE = "WoW Server Simulator — Dashboard"

        BINDINGS = [
            ("q", "quit", "Quit"),
            ("r", "refresh", "Refresh"),
            ("s", "spawn_clients", "Spawn"),
            ("k", "despawn_clients", "Despawn"),
            ("a", "activate_fault", "Activate"),
            ("d", "deactivate_fault", "Deactivate"),
            ("x", "deactivate_all", "Deact All"),
            ("p", "run_pipeline", "Pipeline"),
        ]

        def __init__(self, config: DashboardConfig) -> None:
            super().__init__()
            self._config = config
            self._last_entry_ts: datetime | None = None
            self._fault_list: list = []
            self._player_count: int = 0
            self._health_status: str = "healthy"
            self._pipeline_ran: bool = False
            self._spawn_stop_event: threading.Event | None = None
            self._spawn_active: bool = False

        def compose(self) -> ComposeResult:
            """Build the widget tree."""
            yield Header()
            yield Static("Loading...", id="status-bar")
            with Horizontal(id="panels"):
                yield Static("TICK METRICS\n\nLoading...", id="tick-panel")
                yield Static("GAME MECHANICS\n\nLoading...", id="game-panel")
                yield DataTable(id="zone-table")
            yield Static("FAULT CONTROL\n\nLoading...", id="fault-panel")
            yield RichLog(id="event-log", highlight=True, markup=True)
            yield Static("Loading...", id="suggestion-bar")
            yield Footer()

        def on_mount(self) -> None:
            """Initialize zone table columns and start refresh timer."""
            table = self.query_one("#zone-table", DataTable)
            table.add_columns(*ZONE_COLUMNS)
            self.set_interval(self._config.refresh_interval, self._trigger_refresh)
            self._trigger_refresh()

        def _trigger_refresh(self) -> None:
            """Kick off both health and fault data fetches."""
            self._fetch_health_data()
            self._fetch_fault_list()

        @work(exclusive=True, thread=True)
        def _fetch_health_data(self) -> None:
            """Fetch health data in a worker thread (sync I/O)."""
            try:
                from wowsim.game_metrics import aggregate_game_mechanics
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
                game_mech = aggregate_game_mechanics(entries) if entries else None
                status = determine_status(
                    tick, zones, anomalies,
                    game_mechanics=game_mech,
                    connected_players=players,
                )
                reachable = check_server_reachable(
                    self._config.host, self._config.control_port, timeout=1.0
                )
                uptime = tick.total_ticks if tick else 0

                new_entries, new_ts = filter_new_entries(entries, self._last_entry_ts)

                self.call_from_thread(
                    self._update_health_ui,
                    status, reachable, players, uptime, tick, zones,
                    new_entries, new_ts, game_mech,
                )
            except Exception as exc:
                self.call_from_thread(
                    self.notify, f"Health fetch error: {exc}", severity="error"
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
            game_mechanics: GameMechanicSummary | None = None,
        ) -> None:
            """Update UI widgets with health data (runs on main thread)."""
            # Status bar
            status_bar = self.query_one("#status-bar", Static)
            status_bar.update(format_status_bar(status, reachable, players, uptime))

            # Tick panel
            tick_panel = self.query_one("#tick-panel", Static)
            tick_panel.update(f"TICK METRICS\n\n{format_tick_panel(tick)}")

            # Game mechanics panel
            game_panel = self.query_one("#game-panel", Static)
            game_content = f"GAME MECHANICS\n\n{format_game_mechanics_panel(game_mechanics)}"
            if game_mechanics and game_mechanics.top_damage_dealers:
                threat_text = format_threat_table_panel(game_mechanics.top_damage_dealers)
                game_content += f"\n\nTHREAT TABLE\n{threat_text}"
            game_panel.update(game_content)

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
                    str(z.total_casts),
                    f"{z.zone_dps:.1f}",
                )

            # Event log — filter out noise (tick metrics + control channel chatter)
            if new_entries:
                self._last_entry_ts = new_ts
                log = self.query_one("#event-log", RichLog)
                for entry in new_entries:
                    if entry.type == "metric":
                        continue
                    if entry.component == "control_channel":
                        continue
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
                spawn_active=self._spawn_active,
            )
            bar = self.query_one("#suggestion-bar", Static)
            bar.update(text)

        @work(exclusive=True, thread=True)
        def _fetch_fault_list(self) -> None:
            """Fetch fault list via sync wrapper in a worker thread."""
            try:
                from wowsim.fault_trigger import list_all_faults

                try:
                    resp = list_all_faults(
                        self._config.host, self._config.control_port
                    )
                    faults = resp.faults or []
                except OSError:
                    faults = self._fault_list

                self._fault_list = faults

                def _update() -> None:
                    fault_panel = self.query_one("#fault-panel", Static)
                    fault_panel.update(
                        f"FAULT CONTROL\n\n{_format_fault_table(faults)}"
                    )
                    self._update_suggestion()

                self.call_from_thread(_update)
            except Exception as exc:
                self.call_from_thread(
                    self.notify, f"Fault list error: {exc}", severity="error"
                )

        def action_refresh(self) -> None:
            """Manual refresh triggered by 'r' key."""
            self._trigger_refresh()
            self.notify("Refreshed")

        def action_spawn_clients(self) -> None:
            """Open duration picker, then spawn 5 clients (key: s)."""
            if self._spawn_active:
                self.notify("Clients already active — press k to despawn first", severity="warning")
                return
            self.push_screen(DurationPickerScreen(), self._on_duration_picked)

        def _on_duration_picked(self, duration: float | None) -> None:
            """Handle duration picker result — start spawn with chosen duration."""
            if duration is None:
                return
            self._spawn_stop_event = threading.Event()
            self._spawn_active = True
            self._update_suggestion()
            label = "persistent" if duration == float("inf") else f"{duration:.0f}s"
            self.notify(f"Spawning 5 clients ({label})...")
            self._do_spawn_clients(duration, self._spawn_stop_event)

        @work(thread=True)
        def _do_spawn_clients(
            self, duration: float, stop_event: threading.Event
        ) -> None:
            """Run mock client spawn in a worker thread."""
            try:
                from wowsim.mock_client import run_spawn
                from wowsim.models import ClientConfig

                config = ClientConfig(
                    host=self._config.host,
                    port=self._config.port,
                    duration_seconds=duration,
                )
                result = run_spawn(config, 5, stop_event=stop_event)
                ok = result.successful_connections

                def _finish() -> None:
                    self._spawn_active = False
                    self._spawn_stop_event = None
                    self._update_suggestion()
                    if ok == 0:
                        first_err = next(
                            (c.error for c in result.clients if c.error), "unknown"
                        )
                        self.notify(f"Spawn failed: {first_err}", severity="error")
                    else:
                        self.notify(f"Spawned {ok}/5 clients — finished")
                    self._trigger_refresh()

                self.call_from_thread(_finish)
            except Exception as exc:
                def _error() -> None:
                    self._spawn_active = False
                    self._spawn_stop_event = None
                    self._update_suggestion()
                    self.notify(f"Spawn error: {exc}", severity="error")

                self.call_from_thread(_error)

        def action_despawn_clients(self) -> None:
            """Gracefully stop all running clients (key: k)."""
            if self._spawn_stop_event is not None and self._spawn_active:
                self._spawn_stop_event.set()
                self.notify("Despawning clients...")
            else:
                self.notify("No active clients to despawn", severity="warning")

        def action_activate_fault(self) -> None:
            """Open fault picker modal (key: a)."""
            self.push_screen(FaultPickerScreen(), self._on_fault_picked)

        def _on_fault_picked(self, fault_id: str | None) -> None:
            """Handle fault picker result — activate the chosen fault."""
            if fault_id is None:
                return
            params = FAULT_CATALOG.get(fault_id, {}).get("params", {})
            self._do_activate_fault(fault_id, params)

        @work(thread=True)
        def _do_activate_fault(self, fault_id: str, params: dict) -> None:
            """Activate a fault via sync control client in a worker thread."""
            from wowsim.fault_trigger import activate_fault

            try:
                activate_fault(
                    self._config.host,
                    self._config.control_port,
                    fault_id,
                    params=params,
                )
                self.call_from_thread(self.notify, f"Activated {fault_id}")
            except (OSError, Exception) as exc:
                self.call_from_thread(
                    self.notify, f"Error: {exc}", severity="error"
                )
            self.call_from_thread(self._trigger_refresh)

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

        def action_quit(self) -> None:
            """Signal stop_event before exiting (key: q)."""
            if self._spawn_stop_event is not None:
                self._spawn_stop_event.set()
            self.exit()

        def action_run_pipeline(self) -> None:
            """Run canary deploy pipeline via thread worker (key: p)."""
            # Find first active fault or default to latency-spike
            fault_id = "latency-spike"
            for f in self._fault_list:
                if f.active:
                    fault_id = f.id
                    break
            self.notify(f"Running pipeline for {fault_id}...")
            self._do_run_pipeline(fault_id)

        @work(thread=True)
        def _do_run_pipeline(self, fault_id: str) -> None:
            """Run pipeline in a worker thread."""
            try:
                from wowsim.models import PipelineConfig
                from wowsim.pipeline import format_stage_result, run_pipeline

                config = PipelineConfig(
                    fault_id=fault_id,
                    action="deactivate",
                    control_host=self._config.host,
                    control_port=self._config.control_port,
                    game_host=self._config.host,
                    game_port=self._config.port,
                    log_file=str(self._config.log_file),
                    canary_duration_seconds=6.0,
                    canary_poll_interval_seconds=2.0,
                )
                result = run_pipeline(config)

                def _log_results() -> None:
                    try:
                        log = self.query_one("#event-log", RichLog)
                        outcome = result.outcome.upper()
                        bar = "=" * 40
                        block = [bar, f"  PIPELINE: {outcome}"]
                        for stage in result.stages:
                            tag = "PASS" if stage.passed else "FAIL"
                            block.append(f"  {tag}  {stage.stage} ({stage.duration_seconds:.1f}s)")
                        block.append(bar)
                        log.write("\n".join(block))
                    except Exception as exc:
                        self.notify(f"Log error: {exc}", severity="error")
                    self._pipeline_ran = True
                    self._update_suggestion()
                    self.notify(f"Pipeline: {result.outcome}")

                self.call_from_thread(_log_results)
                self.call_from_thread(self._trigger_refresh)
            except Exception as exc:
                self.call_from_thread(
                    self.notify, f"Pipeline error: {exc}", severity="error"
                )

except ImportError:
    pass
