"""Shared fixtures for wowsim Python tests."""

import json
from datetime import datetime, timezone

import pytest


# --- Sample JSONL lines matching C++ telemetry schema ---

SAMPLE_TIMESTAMP = "2026-02-23T12:00:00.000Z"
SAMPLE_TIMESTAMP_2 = "2026-02-23T12:00:00.050Z"
SAMPLE_TIMESTAMP_3 = "2026-02-23T12:00:00.100Z"


def _make_line(
    type_: str,
    component: str,
    message: str,
    data: dict | None = None,
    timestamp: str = SAMPLE_TIMESTAMP,
) -> str:
    """Build a JSONL telemetry line matching the C++ server format."""
    entry: dict = {
        "v": 1,
        "timestamp": timestamp,
        "type": type_,
        "component": component,
        "message": message,
    }
    if data is not None:
        entry["data"] = data
    return json.dumps(entry)


@pytest.fixture()
def sample_metric_line() -> str:
    """A valid metric JSONL line (game loop tick completed)."""
    return _make_line(
        "metric",
        "game_loop",
        "Tick completed",
        {"tick": 42, "duration_ms": 3.5, "overrun": False},
    )


@pytest.fixture()
def sample_event_line() -> str:
    """A valid event JSONL line (client disconnected)."""
    return _make_line(
        "event",
        "game_server",
        "Client disconnected",
        {"session_id": 7},
    )


@pytest.fixture()
def sample_error_line() -> str:
    """A valid error JSONL line (zone tick exception)."""
    return _make_line(
        "error",
        "zone",
        "Zone tick exception",
        {"zone_id": 1, "error": "segfault simulation"},
    )


@pytest.fixture()
def sample_entry_no_data_line() -> str:
    """A valid JSONL line with no data field."""
    return _make_line("event", "server", "Server shutting down")


@pytest.fixture()
def sample_jsonl(
    sample_metric_line: str,
    sample_event_line: str,
    sample_error_line: str,
) -> str:
    """Multi-line JSONL string with 3 valid entries."""
    return "\n".join([sample_metric_line, sample_event_line, sample_error_line])


@pytest.fixture()
def sample_log_file(tmp_path, sample_jsonl: str):
    """Temp file containing sample JSONL data."""
    path = tmp_path / "telemetry.jsonl"
    path.write_text(sample_jsonl + "\n")
    return path


@pytest.fixture()
def sample_log_file_with_invalid(tmp_path, sample_metric_line: str) -> "Path":
    """Temp file with valid and invalid lines."""
    path = tmp_path / "mixed.jsonl"
    lines = [
        sample_metric_line,
        "NOT VALID JSON",
        '{"v": 1}',  # missing required fields
        sample_metric_line,
    ]
    path.write_text("\n".join(lines) + "\n")
    return path


@pytest.fixture()
def entries_with_anomalies() -> list[str]:
    """JSONL lines containing various anomalies for detection tests."""
    base_ts = "2026-02-23T12:00:00"
    lines = []

    # Normal tick
    lines.append(
        _make_line(
            "metric",
            "game_loop",
            "Tick completed",
            {"tick": 1, "duration_ms": 3.0, "overrun": False},
            f"{base_ts}.000Z",
        )
    )

    # Latency spike — warning (70ms > 60ms threshold)
    lines.append(
        _make_line(
            "metric",
            "game_loop",
            "Tick completed",
            {"tick": 2, "duration_ms": 70.0, "overrun": True},
            f"{base_ts}.050Z",
        )
    )

    # Latency spike — critical (150ms > 100ms threshold)
    lines.append(
        _make_line(
            "metric",
            "game_loop",
            "Tick completed",
            {"tick": 3, "duration_ms": 150.0, "overrun": True},
            f"{base_ts}.100Z",
        )
    )

    # Zone crash
    lines.append(
        _make_line(
            "error",
            "zone",
            "Zone tick exception",
            {"zone_id": 1, "error": "null pointer"},
            f"{base_ts}.150Z",
        )
    )

    # Error burst — 5 errors in quick succession
    for i in range(5):
        lines.append(
            _make_line(
                "error",
                "combat",
                f"Processing error {i}",
                {"detail": f"err_{i}"},
                f"{base_ts}.{200 + i:03d}Z",
            )
        )

    # Unexpected disconnect
    lines.append(
        _make_line(
            "event",
            "game_server",
            "Client disconnected",
            {"session_id": 99},
            f"{base_ts}.300Z",
        )
    )

    return lines
