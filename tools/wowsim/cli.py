"""Main CLI entrypoint for the wowsim tooling suite.

Provides subcommands for fault injection, health monitoring,
log analysis, mock client spawning, and the monitoring dashboard.
"""

import click

from wowsim import __version__


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
def parse_logs() -> None:
    """Parse and analyze server telemetry logs."""
    click.echo("parse-logs: not yet implemented")


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
