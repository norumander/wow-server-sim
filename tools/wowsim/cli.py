"""Main CLI entrypoint for the wowsim tooling suite.

Provides subcommands for fault injection, health monitoring,
log analysis, mock client spawning, and the monitoring dashboard.
"""

from __future__ import annotations

import sys
from pathlib import Path

import click

from wowsim import __version__
from wowsim.log_parser import (
    detect_anomalies,
    filter_entries,
    format_anomalies,
    format_summary,
    parse_file,
    parse_stream,
    summarize,
)
from wowsim.models import ParseResult


@click.group()
@click.version_option(version=__version__, prog_name="wowsim")
def main() -> None:
    """WoW Server Simulator — reliability engineering tools."""


@main.command()
@click.option(
    "--log-file",
    required=True,
    type=click.Path(exists=True),
    help="Path to JSONL telemetry log file.",
)
@click.option("--host", default="localhost", help="Game server host.")
@click.option("--port", default=8080, type=int, help="Game server port.")
@click.option("--control-port", default=8081, type=int, help="Control channel port.")
@click.option("--watch", is_flag=True, help="Continuous monitoring mode.")
@click.option(
    "--interval", default=2, type=int, help="Watch refresh interval (seconds)."
)
@click.option(
    "--format",
    "output_format",
    type=click.Choice(["text", "json"]),
    default="text",
    help="Output format.",
)
@click.option("--no-faults", is_flag=True, help="Skip control channel fault query.")
def health(
    log_file: str,
    host: str,
    port: int,
    control_port: int,
    watch: bool,
    interval: int,
    output_format: str,
    no_faults: bool,
) -> None:
    """Check server health status."""
    import time

    from wowsim.health_check import build_health_report, format_health_report

    log_path = Path(log_file)

    while True:
        report = build_health_report(
            log_path=log_path,
            game_host=host,
            game_port=port,
            control_host=host,
            control_port=control_port,
            skip_faults=no_faults,
        )

        if output_format == "json":
            click.echo(report.model_dump_json(indent=2))
        else:
            click.echo(format_health_report(report))

        if not watch:
            break

        time.sleep(interval)
        click.clear()


@main.group("inject-fault")
@click.option("--host", default="localhost", help="Control channel host.")
@click.option("--port", default=8081, type=int, help="Control channel port.")
@click.pass_context
def inject_fault(ctx: click.Context, host: str, port: int) -> None:
    """Inject fault scenarios into the running server."""
    ctx.ensure_object(dict)
    ctx.obj["host"] = host
    ctx.obj["port"] = port


@inject_fault.command()
@click.argument("fault_id")
@click.option("--delay-ms", type=int, default=None, help="Latency spike delay (ms).")
@click.option("--megabytes", type=int, default=None, help="Memory pressure size (MB).")
@click.option("--multiplier", type=int, default=None, help="Event flood multiplier.")
@click.option("--duration", default=None, help="Duration: e.g. '5s' or '100t'.")
@click.option(
    "--zone", "target_zone_id", type=int, default=0, help="Target zone (0=all)."
)
@click.pass_context
def activate(
    ctx: click.Context,
    fault_id: str,
    delay_ms: int | None,
    megabytes: int | None,
    multiplier: int | None,
    duration: str | None,
    target_zone_id: int,
) -> None:
    """Activate a fault by ID (e.g. latency-spike, session-crash)."""
    from wowsim.fault_trigger import (
        ControlClientError,
        activate_fault,
        parse_duration,
    )

    params: dict[str, int] = {}
    if delay_ms is not None:
        params["delay_ms"] = delay_ms
    if megabytes is not None:
        params["megabytes"] = megabytes
    if multiplier is not None:
        params["multiplier"] = multiplier

    duration_ticks = 0
    if duration is not None:
        try:
            duration_ticks = parse_duration(duration)
        except ValueError as exc:
            raise click.ClickException(str(exc))

    try:
        resp = activate_fault(
            ctx.obj["host"],
            ctx.obj["port"],
            fault_id,
            params=params,
            target_zone_id=target_zone_id,
            duration_ticks=duration_ticks,
        )
        click.echo(f"Activated {resp.fault_id}")
    except ControlClientError as exc:
        raise click.ClickException(str(exc))
    except OSError as exc:
        raise click.ClickException(f"Connection error: {exc}")


@inject_fault.command()
@click.argument("fault_id")
@click.pass_context
def deactivate(ctx: click.Context, fault_id: str) -> None:
    """Deactivate a specific fault by ID."""
    from wowsim.fault_trigger import ControlClientError, deactivate_fault

    try:
        resp = deactivate_fault(ctx.obj["host"], ctx.obj["port"], fault_id)
        click.echo(f"Deactivated {resp.fault_id}")
    except ControlClientError as exc:
        raise click.ClickException(str(exc))
    except OSError as exc:
        raise click.ClickException(f"Connection error: {exc}")


@inject_fault.command("deactivate-all")
@click.pass_context
def deactivate_all(ctx: click.Context) -> None:
    """Deactivate all active faults."""
    from wowsim.fault_trigger import ControlClientError, deactivate_all_faults

    try:
        deactivate_all_faults(ctx.obj["host"], ctx.obj["port"])
        click.echo("All faults deactivated")
    except ControlClientError as exc:
        raise click.ClickException(str(exc))
    except OSError as exc:
        raise click.ClickException(f"Connection error: {exc}")


@inject_fault.command()
@click.argument("fault_id")
@click.pass_context
def status(ctx: click.Context, fault_id: str) -> None:
    """Show status of a specific fault."""
    from wowsim.fault_trigger import ControlClientError, fault_status, format_fault_info

    try:
        resp = fault_status(ctx.obj["host"], ctx.obj["port"], fault_id)
        if resp.status:
            click.echo(format_fault_info(resp.status))
        else:
            click.echo(f"No status returned for {fault_id}")
    except ControlClientError as exc:
        raise click.ClickException(str(exc))
    except OSError as exc:
        raise click.ClickException(f"Connection error: {exc}")


@inject_fault.command("list")
@click.pass_context
def list_faults(ctx: click.Context) -> None:
    """List all registered faults and their status."""
    from wowsim.fault_trigger import (
        ControlClientError,
        format_fault_list,
        list_all_faults,
    )

    try:
        resp = list_all_faults(ctx.obj["host"], ctx.obj["port"])
        if resp.faults:
            click.echo(format_fault_list(resp.faults))
        else:
            click.echo("No faults registered")
    except ControlClientError as exc:
        raise click.ClickException(str(exc))
    except OSError as exc:
        raise click.ClickException(f"Connection error: {exc}")


@main.command("parse-logs")
@click.argument("file", type=click.Path(exists=False))
@click.option("--type", "type_filter", default=None, help="Filter by entry type.")
@click.option(
    "--component", "component_filter", default=None, help="Filter by component."
)
@click.option(
    "--message", "message_filter", default=None, help="Filter by message substring."
)
@click.option("--anomalies", is_flag=True, help="Show detected anomalies only.")
@click.option(
    "--format",
    "output_format",
    type=click.Choice(["text", "json"]),
    default="text",
    help="Output format.",
)
def parse_logs(
    file: str,
    type_filter: str | None,
    component_filter: str | None,
    message_filter: str | None,
    anomalies: bool,
    output_format: str,
) -> None:
    """Parse and analyze server telemetry logs.

    FILE is the path to a JSONL telemetry file (use - for stdin).
    """
    if file == "-":
        entries = parse_stream(sys.stdin)
    else:
        path = Path(file)
        if not path.exists():
            raise click.ClickException(f"File not found: {file}")
        entries = parse_file(path)

    entries = filter_entries(
        entries,
        type_filter=type_filter,
        component_filter=component_filter,
        message_filter=message_filter,
    )

    summary = summarize(entries)
    detected = detect_anomalies(entries)

    if output_format == "json":
        result = ParseResult(entries=entries, summary=summary, anomalies=detected)
        click.echo(result.model_dump_json(indent=2))
    elif anomalies:
        click.echo(format_anomalies(detected))
    else:
        click.echo(format_summary(summary))
        if detected:
            click.echo("")
            click.echo(format_anomalies(detected))


@main.command("spawn-clients")
def spawn_clients() -> None:
    """Spawn simulated player clients."""
    click.echo("spawn-clients: not yet implemented")


@main.command()
def dashboard() -> None:
    """Launch the monitoring dashboard."""
    click.echo("dashboard: not yet implemented")


if __name__ == "__main__":
    main()
