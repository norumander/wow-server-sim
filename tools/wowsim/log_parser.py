"""Log parser for server telemetry — parsing, filtering, summarizing, anomaly detection."""

from __future__ import annotations

import json

from pydantic import ValidationError

from wowsim.models import TelemetryEntry


def parse_line(line: str) -> TelemetryEntry | None:
    """Parse a single JSON telemetry line into a TelemetryEntry, or None if invalid."""
    line = line.strip()
    if not line:
        return None
    try:
        raw = json.loads(line)
        return TelemetryEntry.model_validate(raw)
    except (json.JSONDecodeError, ValidationError):
        return None
