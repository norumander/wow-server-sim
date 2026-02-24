"""Log parser for server telemetry — parsing, filtering, summarizing, anomaly detection."""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import TextIO

from pydantic import ValidationError

from wowsim.models import Anomaly, LogSummary, TelemetryEntry

# --- Anomaly detection thresholds ---
DEFAULT_TICK_DURATION_WARN_MS = 60.0
DEFAULT_TICK_DURATION_CRIT_MS = 100.0
DEFAULT_ERROR_BURST_THRESHOLD = 5
DEFAULT_ERROR_BURST_WINDOW_SEC = 10.0


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


def parse_file(path: Path) -> list[TelemetryEntry]:
    """Parse all valid telemetry entries from a JSONL file."""
    with open(path) as f:
        return parse_stream(f)


def parse_stream(stream: TextIO) -> list[TelemetryEntry]:
    """Parse all valid telemetry entries from a text stream."""
    entries: list[TelemetryEntry] = []
    for line in stream:
        entry = parse_line(line)
        if entry is not None:
            entries.append(entry)
    return entries


def filter_entries(
    entries: list[TelemetryEntry],
    type_filter: str | None = None,
    component_filter: str | None = None,
    message_filter: str | None = None,
) -> list[TelemetryEntry]:
    """Filter entries by type, component, and/or message substring."""
    result = entries
    if type_filter is not None:
        result = [e for e in result if e.type == type_filter]
    if component_filter is not None:
        result = [e for e in result if e.component == component_filter]
    if message_filter is not None:
        result = [e for e in result if message_filter in e.message]
    return result


def summarize(entries: list[TelemetryEntry]) -> LogSummary:
    """Compute aggregate statistics from a list of telemetry entries."""
    type_counts: Counter[str] = Counter()
    component_counts: Counter[str] = Counter()
    error_count = 0
    timestamps: list = []

    for entry in entries:
        type_counts[entry.type] += 1
        component_counts[entry.component] += 1
        if entry.type == "error":
            error_count += 1
        timestamps.append(entry.timestamp)

    if timestamps:
        start = min(timestamps)
        end = max(timestamps)
        duration = (end - start).total_seconds()
    else:
        start = None
        end = None
        duration = 0.0

    return LogSummary(
        total_entries=len(entries),
        entries_by_type=dict(type_counts),
        entries_by_component=dict(component_counts),
        error_count=error_count,
        time_range_start=start,
        time_range_end=end,
        duration_seconds=duration,
    )


# --- Anomaly Detection ---


def detect_anomalies(
    entries: list[TelemetryEntry],
    tick_warn_ms: float = DEFAULT_TICK_DURATION_WARN_MS,
    tick_crit_ms: float = DEFAULT_TICK_DURATION_CRIT_MS,
    error_burst_threshold: int = DEFAULT_ERROR_BURST_THRESHOLD,
    error_burst_window_sec: float = DEFAULT_ERROR_BURST_WINDOW_SEC,
) -> list[Anomaly]:
    """Detect anomalies in the telemetry stream."""
    anomalies: list[Anomaly] = []
    anomalies.extend(_detect_latency_spikes(entries, tick_warn_ms, tick_crit_ms))
    anomalies.extend(_detect_zone_crashes(entries))
    anomalies.extend(
        _detect_error_bursts(entries, error_burst_threshold, error_burst_window_sec)
    )
    anomalies.extend(_detect_unexpected_disconnects(entries))
    return anomalies


def _detect_latency_spikes(
    entries: list[TelemetryEntry],
    warn_ms: float,
    crit_ms: float,
) -> list[Anomaly]:
    """Detect tick duration anomalies from game_loop 'Tick completed' metrics."""
    anomalies: list[Anomaly] = []
    for entry in entries:
        if (
            entry.type == "metric"
            and entry.component == "game_loop"
            and entry.message == "Tick completed"
        ):
            duration = entry.data.get("duration_ms", 0.0)
            if duration >= crit_ms:
                anomalies.append(
                    Anomaly(
                        type="latency_spike",
                        severity="critical",
                        timestamp=entry.timestamp,
                        message=f"Tick duration {duration}ms exceeds critical threshold ({crit_ms}ms)",
                        details={"duration_ms": duration, "threshold_ms": crit_ms},
                    )
                )
            elif duration >= warn_ms:
                anomalies.append(
                    Anomaly(
                        type="latency_spike",
                        severity="warning",
                        timestamp=entry.timestamp,
                        message=f"Tick duration {duration}ms exceeds warning threshold ({warn_ms}ms)",
                        details={"duration_ms": duration, "threshold_ms": warn_ms},
                    )
                )
    return anomalies


def _detect_zone_crashes(entries: list[TelemetryEntry]) -> list[Anomaly]:
    """Detect zone tick exceptions."""
    anomalies: list[Anomaly] = []
    for entry in entries:
        if (
            entry.type == "error"
            and entry.component == "zone"
            and entry.message == "Zone tick exception"
        ):
            zone_id = entry.data.get("zone_id", "unknown")
            error_msg = entry.data.get("error", "unknown error")
            anomalies.append(
                Anomaly(
                    type="zone_crash",
                    severity="critical",
                    timestamp=entry.timestamp,
                    message=f"Zone {zone_id} crashed: {error_msg}",
                    details=dict(entry.data),
                )
            )
    return anomalies


def _detect_error_bursts(
    entries: list[TelemetryEntry],
    threshold: int,
    window_sec: float,
) -> list[Anomaly]:
    """Detect bursts of errors within a sliding time window."""
    errors = [e for e in entries if e.type == "error"]
    if len(errors) < threshold:
        return []

    anomalies: list[Anomaly] = []
    # Sliding window: for each error, count how many errors fall within window_sec
    reported = False
    for i, error in enumerate(errors):
        window_start = error.timestamp
        count = 0
        for j in range(i, len(errors)):
            delta = (errors[j].timestamp - window_start).total_seconds()
            if delta <= window_sec:
                count += 1
            else:
                break
        if count >= threshold and not reported:
            anomalies.append(
                Anomaly(
                    type="error_burst",
                    severity="critical",
                    timestamp=error.timestamp,
                    message=f"{count} errors within {window_sec}s (threshold: {threshold})",
                    details={"error_count": count, "window_sec": window_sec},
                )
            )
            reported = True
    return anomalies


def _detect_unexpected_disconnects(entries: list[TelemetryEntry]) -> list[Anomaly]:
    """Detect unexpected client disconnections."""
    anomalies: list[Anomaly] = []
    for entry in entries:
        if (
            entry.type == "event"
            and entry.component == "game_server"
            and entry.message == "Client disconnected"
        ):
            session_id = entry.data.get("session_id", "unknown")
            anomalies.append(
                Anomaly(
                    type="unexpected_disconnect",
                    severity="warning",
                    timestamp=entry.timestamp,
                    message=f"Client session {session_id} disconnected unexpectedly",
                    details=dict(entry.data),
                )
            )
    return anomalies
