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
def health() -> None:
    """Check server health status."""
    click.echo("health: not yet implemented")


@main.command("inject-fault")
def inject_fault() -> None:
    """Inject a fault scenario into the running server."""
    click.echo("inject-fault: not yet implemented")


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
