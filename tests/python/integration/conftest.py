"""Shared fixtures for integration tests.

Provides telemetry log file generators for different fault scenarios,
reusing the mock servers from the parent conftest.py.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Telemetry line helpers
# ---------------------------------------------------------------------------

BASE_TS = "2026-02-24T12:00:00"


def _ts(offset_ms: int) -> str:
    """Generate ISO 8601 timestamp with millisecond offset from BASE_TS."""
    total_s, ms = divmod(offset_ms, 1000)
    m, s = divmod(total_s, 60)
    return f"2026-02-24T12:{m:02d}:{s:02d}.{ms:03d}Z"


def _entry(
    type_: str,
    component: str,
    message: str,
    data: dict | None = None,
    ts: str = f"{BASE_TS}.000Z",
) -> str:
    """Build a JSONL telemetry line."""
    entry: dict = {
        "v": 1,
        "timestamp": ts,
        "type": type_,
        "component": component,
        "message": message,
    }
    if data is not None:
        entry["data"] = data
    return json.dumps(entry)


def make_tick_line(
    tick: int, duration_ms: float, overrun: bool, ts: str
) -> str:
    """Build a game_loop tick completed metric line."""
    return _entry(
        "metric",
        "game_loop",
        "Tick completed",
        {"tick": tick, "duration_ms": duration_ms, "overrun": overrun},
        ts,
    )


def make_connection_line(session_id: int, ts: str) -> str:
    """Build a connection accepted event line."""
    return _entry(
        "event",
        "game_server",
        "Connection accepted",
        {"session_id": session_id},
        ts,
    )


def make_disconnect_line(session_id: int, ts: str) -> str:
    """Build a client disconnected event line."""
    return _entry(
        "event",
        "game_server",
        "Client disconnected",
        {"session_id": session_id},
        ts,
    )


def make_zone_tick_line(
    zone_id: int, events: int, duration_ms: float, ts: str
) -> str:
    """Build a zone tick completed metric line."""
    return _entry(
        "metric",
        "zone",
        "Zone tick completed",
        {"zone_id": zone_id, "events_processed": events, "duration_ms": duration_ms},
        ts,
    )


def make_zone_error_line(zone_id: int, error: str, ts: str) -> str:
    """Build a zone tick exception error line."""
    return _entry(
        "error",
        "zone",
        "Zone tick exception",
        {"zone_id": zone_id, "error": error},
        ts,
    )


def make_combat_error_line(detail: str, ts: str) -> str:
    """Build a combat processing error line."""
    return _entry(
        "error",
        "combat",
        "Processing error",
        {"detail": detail},
        ts,
    )


def write_log(tmp_path: Path, filename: str, lines: list[str]) -> Path:
    """Write lines to a JSONL file and return the path."""
    path = tmp_path / filename
    path.write_text("\n".join(lines) + "\n")
    return path


# ---------------------------------------------------------------------------
# Telemetry scenario fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def normal_operation_log(tmp_path: Path) -> Path:
    """Healthy steady-state: 20 normal ticks, 5 connections, 2 disconnections,
    zone 1+2 ticks, no errors."""
    lines: list[str] = []

    # 20 normal ticks (3-5ms, no overrun)
    for i in range(1, 21):
        lines.append(make_tick_line(i, 3.0 + (i % 3) * 0.5, False, _ts((i - 1) * 50)))

    # 5 connections
    for sid in range(1, 6):
        lines.append(make_connection_line(sid, _ts(1000 + sid * 10)))

    # 2 disconnections (sessions 1 and 2)
    lines.append(make_disconnect_line(1, _ts(1100)))
    lines.append(make_disconnect_line(2, _ts(1110)))

    # Zone ticks (zone 1 and zone 2)
    lines.append(make_zone_tick_line(1, 5, 3.2, _ts(1200)))
    lines.append(make_zone_tick_line(2, 3, 2.8, _ts(1210)))

    return write_log(tmp_path, "normal_operation.jsonl", lines)


@pytest.fixture()
def latency_spike_log(tmp_path: Path) -> Path:
    """F1 fault active: 10 normal ticks, 3 spike ticks (70ms/150ms/200ms),
    7 normal ticks, 3 connections."""
    lines: list[str] = []

    # 10 normal ticks
    for i in range(1, 11):
        lines.append(make_tick_line(i, 3.0, False, _ts((i - 1) * 50)))

    # 3 spike ticks
    lines.append(make_tick_line(11, 70.0, True, _ts(500)))
    lines.append(make_tick_line(12, 150.0, True, _ts(550)))
    lines.append(make_tick_line(13, 200.0, True, _ts(600)))

    # 7 normal ticks
    for i in range(14, 21):
        lines.append(make_tick_line(i, 3.0, False, _ts(650 + (i - 14) * 50)))

    # 3 connections
    for sid in range(1, 4):
        lines.append(make_connection_line(sid, _ts(1000 + sid * 10)))

    return write_log(tmp_path, "latency_spike.jsonl", lines)


@pytest.fixture()
def session_crash_log(tmp_path: Path) -> Path:
    """F2 fault active: 10 normal ticks, 5 connections, 3 sudden disconnects,
    5 more ticks."""
    lines: list[str] = []

    # 10 normal ticks
    for i in range(1, 11):
        lines.append(make_tick_line(i, 3.0, False, _ts((i - 1) * 50)))

    # 5 connections
    for sid in range(1, 6):
        lines.append(make_connection_line(sid, _ts(500 + sid * 10)))

    # 3 sudden disconnects
    for sid in range(1, 4):
        lines.append(make_disconnect_line(sid, _ts(600 + sid * 10)))

    # 5 more ticks
    for i in range(11, 16):
        lines.append(make_tick_line(i, 3.0, False, _ts(700 + (i - 11) * 50)))

    return write_log(tmp_path, "session_crash.jsonl", lines)


@pytest.fixture()
def zone_crash_log(tmp_path: Path) -> Path:
    """Zone failure: 10 ticks, zone 1+2 ticks, zone 1 crash error, 5 more ticks."""
    lines: list[str] = []

    # 10 normal ticks
    for i in range(1, 11):
        lines.append(make_tick_line(i, 3.0, False, _ts((i - 1) * 50)))

    # Zone ticks
    lines.append(make_zone_tick_line(1, 5, 3.2, _ts(500)))
    lines.append(make_zone_tick_line(2, 3, 2.8, _ts(510)))

    # Zone 1 crash
    lines.append(make_zone_error_line(1, "segfault simulation", _ts(520)))

    # 5 more ticks
    for i in range(11, 16):
        lines.append(make_tick_line(i, 3.0, False, _ts(550 + (i - 11) * 50)))

    return write_log(tmp_path, "zone_crash.jsonl", lines)


@pytest.fixture()
def recovery_scenario_log(tmp_path: Path) -> Path:
    """Full arc: Healthy (ticks 1-10) -> fault (ticks 11-15, overruns + zone crash)
    -> recovery (ticks 16-25)."""
    lines: list[str] = []

    # Phase 1: Healthy (ticks 1-10)
    for i in range(1, 11):
        lines.append(make_tick_line(i, 3.0, False, _ts((i - 1) * 50)))

    # Phase 2: Fault (ticks 11-15, critical latency + zone crash)
    lines.append(make_tick_line(11, 150.0, True, _ts(500)))
    lines.append(make_tick_line(12, 200.0, True, _ts(550)))
    lines.append(make_tick_line(13, 120.0, True, _ts(600)))
    lines.append(make_zone_error_line(1, "null pointer", _ts(625)))
    lines.append(make_tick_line(14, 150.0, True, _ts(650)))
    lines.append(make_tick_line(15, 180.0, True, _ts(700)))

    # Phase 3: Recovery (ticks 16-25)
    for i in range(16, 26):
        lines.append(make_tick_line(i, 3.0, False, _ts(750 + (i - 16) * 50)))

    return write_log(tmp_path, "recovery_scenario.jsonl", lines)


@pytest.fixture()
def fifty_player_log(tmp_path: Path) -> Path:
    """50-player load: 50 connections, 20 normal ticks, 3 zone ticks, 0 errors."""
    lines: list[str] = []

    # 50 connections
    for sid in range(1, 51):
        lines.append(make_connection_line(sid, _ts(sid)))

    # 20 normal ticks
    for i in range(1, 21):
        lines.append(make_tick_line(i, 3.0, False, _ts(100 + (i - 1) * 50)))

    # 3 zone ticks
    lines.append(make_zone_tick_line(1, 20, 3.0, _ts(1200)))
    lines.append(make_zone_tick_line(2, 15, 2.5, _ts(1210)))
    lines.append(make_zone_tick_line(3, 15, 2.8, _ts(1220)))

    return write_log(tmp_path, "fifty_player.jsonl", lines)
