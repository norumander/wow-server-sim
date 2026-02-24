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
@click.option("--count", default=10, type=int, help="Number of clients to spawn.")
@click.option("--duration", default=10.0, type=float, help="Seconds per client.")
@click.option("--host", default="localhost", help="Game server host.")
@click.option("--port", default=8080, type=int, help="Game server port.")
@click.option("--rate", default=2.0, type=float, help="Actions per second per client.")
@click.option(
    "--format",
    "output_format",
    type=click.Choice(["text", "json"]),
    default="text",
    help="Output format.",
)
def spawn_clients(
    count: int,
    duration: float,
    host: str,
    port: int,
    rate: float,
    output_format: str,
) -> None:
    """Spawn simulated player clients that generate game traffic."""
    from wowsim.mock_client import format_spawn_result, run_spawn
    from wowsim.models import ClientConfig

    config = ClientConfig(
        host=host,
        port=port,
        actions_per_second=rate,
        duration_seconds=duration,
    )
    result = run_spawn(config, count)

    if output_format == "json":
        click.echo(result.model_dump_json(indent=2))
    else:
        click.echo(format_spawn_result(result))


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
@click.option(
    "--refresh", default=2.0, type=float, help="Refresh interval in seconds."
)
def dashboard(
    log_file: str,
    host: str,
    port: int,
    control_port: int,
    refresh: float,
) -> None:
    """Launch the monitoring dashboard."""
    from wowsim.dashboard import DashboardConfig, WoWDashboardApp

    config = DashboardConfig(
        log_file=Path(log_file),
        host=host,
        port=port,
        control_port=control_port,
        refresh_interval=refresh,
    )
    WoWDashboardApp(config).run()


@main.command()
@click.option("--fault-id", required=True, help="Fault to deploy.")
@click.option(
    "--action",
    type=click.Choice(["activate", "deactivate"]),
    default="activate",
    help="Deploy action.",
)
@click.option("--version", "deploy_version", default="1.0.0", help="Deploy version.")
@click.option(
    "--canary-duration", default=10.0, type=float, help="Canary duration (seconds)."
)
@click.option(
    "--canary-interval", default=2.0, type=float, help="Canary poll interval (seconds)."
)
@click.option(
    "--rollback-on",
    type=click.Choice(["critical", "degraded"]),
    default="critical",
    help="Rollback threshold.",
)
@click.option("--log-file", default=None, type=click.Path(), help="Telemetry log file.")
@click.option("--host", default="localhost", help="Game server host.")
@click.option("--port", default=8080, type=int, help="Game server port.")
@click.option("--control-port", default=8081, type=int, help="Control channel port.")
@click.option("--delay-ms", type=int, default=None, help="Latency spike delay (ms).")
@click.option("--megabytes", type=int, default=None, help="Memory pressure size (MB).")
@click.option("--multiplier", type=int, default=None, help="Event flood multiplier.")
@click.option("--duration", default=None, help="Fault duration: '5s' or '100t'.")
@click.option(
    "--zone", "target_zone_id", type=int, default=0, help="Target zone (0=all)."
)
@click.option(
    "--format",
    "output_format",
    type=click.Choice(["text", "json"]),
    default="text",
    help="Output format.",
)
def deploy(
    fault_id: str,
    action: str,
    deploy_version: str,
    canary_duration: float,
    canary_interval: float,
    rollback_on: str,
    log_file: str | None,
    host: str,
    port: int,
    control_port: int,
    delay_ms: int | None,
    megabytes: int | None,
    multiplier: int | None,
    duration: str | None,
    target_zone_id: int,
    output_format: str,
) -> None:
    """Run a hotfix deployment pipeline (build -> validate -> canary -> promote/rollback)."""
    from wowsim.fault_trigger import parse_duration
    from wowsim.models import PipelineConfig
    from wowsim.pipeline import format_pipeline_result, run_pipeline

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

    config = PipelineConfig(
        version=deploy_version,
        fault_id=fault_id,
        action=action,
        params=params,
        target_zone_id=target_zone_id,
        duration_ticks=duration_ticks,
        canary_duration_seconds=canary_duration,
        canary_poll_interval_seconds=canary_interval,
        rollback_on=rollback_on,
        game_host=host,
        game_port=port,
        control_host=host,
        control_port=control_port,
        log_file=log_file,
    )

    result = run_pipeline(config)

    if output_format == "json":
        click.echo(result.model_dump_json(indent=2))
    else:
        click.echo(format_pipeline_result(result))


@main.command()
@click.option(
    "--log-file",
    required=True,
    type=click.Path(exists=True),
    help="Path to JSONL telemetry log file.",
)
@click.option("--host", default="localhost", help="Game server host.")
@click.option("--port", default=8080, type=int, help="Game server port.")
@click.option(
    "--counts",
    default="0,10,25,50,100",
    help="Comma-separated client counts to benchmark.",
)
@click.option("--duration", default=10.0, type=float, help="Seconds per scenario.")
@click.option("--rate", default=2.0, type=float, help="Actions per second per client.")
@click.option(
    "--settle", default=2.0, type=float, help="Settle time between scenarios (seconds)."
)
@click.option(
    "--max-avg-tick", default=50.0, type=float, help="Max avg tick duration (ms)."
)
@click.option(
    "--max-p99-tick", default=100.0, type=float, help="Max P99 tick duration (ms)."
)
@click.option(
    "--max-overrun-pct", default=5.0, type=float, help="Max tick overrun percentage."
)
@click.option(
    "--format",
    "output_format",
    type=click.Choice(["text", "json"]),
    default="text",
    help="Output format.",
)
def benchmark(
    log_file: str,
    host: str,
    port: int,
    counts: str,
    duration: float,
    rate: float,
    settle: float,
    max_avg_tick: float,
    max_p99_tick: float,
    max_overrun_pct: float,
    output_format: str,
) -> None:
    """Run performance benchmarks with scaling client counts."""
    from wowsim.benchmark import format_benchmark_result, run_benchmark
    from wowsim.models import BenchmarkConfig

    client_counts = [int(c.strip()) for c in counts.split(",")]

    config = BenchmarkConfig(
        game_host=host,
        game_port=port,
        log_file=log_file,
        client_counts=client_counts,
        duration_seconds=duration,
        actions_per_second=rate,
        settle_seconds=settle,
        max_avg_tick_ms=max_avg_tick,
        max_p99_tick_ms=max_p99_tick,
        max_overrun_pct=max_overrun_pct,
    )

    result = run_benchmark(config)

    if output_format == "json":
        click.echo(result.model_dump_json(indent=2))
    else:
        click.echo(format_benchmark_result(result))

    sys.exit(0 if result.overall_passed else 1)


if __name__ == "__main__":
    main()
