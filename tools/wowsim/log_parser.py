"""Log parser for server telemetry — parsing, filtering, summarizing, anomaly detection."""

from wowsim.models import TelemetryEntry


def parse_line(line: str) -> TelemetryEntry | None:
    """Parse a single JSON telemetry line into a TelemetryEntry, or None if invalid."""
    return None
