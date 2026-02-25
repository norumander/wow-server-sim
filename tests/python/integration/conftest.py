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
    zone_id: int,
    events: int,
    duration_ms: float,
    ts: str,
    casts_started: int = 0,
    total_damage_dealt: int = 0,
    attacks_processed: int = 0,
) -> str:
    """Build a zone tick completed metric line.

    Optional game-mechanic params are only included when non-zero,
    preserving backward compatibility with existing fixtures.
    """
    data: dict = {
        "zone_id": zone_id,
        "events_processed": events,
        "duration_ms": duration_ms,
    }
    if casts_started:
        data["casts_started"] = casts_started
    if total_damage_dealt:
        data["total_damage_dealt"] = total_damage_dealt
    if attacks_processed:
        data["attacks_processed"] = attacks_processed
    return _entry("metric", "zone", "Zone tick completed", data, ts)


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


def make_cast_started_line(session_id: int, spell_id: int, ts: str) -> str:
    """Build a spellcast 'Cast started' event line."""
    return _entry(
        "event",
        "spellcast",
        "Cast started",
        {"session_id": session_id, "spell_id": spell_id, "cast_time_ticks": 20, "instant": False},
        ts,
    )


def make_cast_completed_line(session_id: int, spell_id: int, ts: str) -> str:
    """Build a spellcast 'Cast completed' event line."""
    return _entry(
        "event",
        "spellcast",
        "Cast completed",
        {"session_id": session_id, "spell_id": spell_id},
        ts,
    )


def make_cast_interrupted_line(
    session_id: int, spell_id: int, reason: str, ts: str
) -> str:
    """Build a spellcast 'Cast interrupted' event line."""
    return _entry(
        "event",
        "spellcast",
        "Cast interrupted",
        {"session_id": session_id, "spell_id": spell_id, "reason": reason},
        ts,
    )


def make_gcd_blocked_line(session_id: int, spell_id: int, ts: str) -> str:
    """Build a spellcast 'Cast blocked by GCD' event line."""
    return _entry(
        "event",
        "spellcast",
        "Cast blocked by GCD",
        {"session_id": session_id, "spell_id": spell_id},
        ts,
    )


def make_damage_dealt_line(
    attacker_id: int, target_id: int, damage: int, ts: str
) -> str:
    """Build a combat 'Damage dealt' event line."""
    return _entry(
        "event",
        "combat",
        "Damage dealt",
        {"attacker_id": attacker_id, "target_id": target_id, "actual_damage": damage, "damage_type": "physical"},
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
    """F2 fault active: 10 normal ticks, 5 connections, 3 unexpected disconnects
    (sessions that were never connected in this window), 5 more ticks.

    The unexpected disconnects simulate a session-crash fault: the server
    force-terminates sessions that connected in a prior analysis window,
    so only the disconnect event appears — no matching connection.
    """
    lines: list[str] = []

    # 10 normal ticks
    for i in range(1, 11):
        lines.append(make_tick_line(i, 3.0, False, _ts((i - 1) * 50)))

    # 5 connections (sessions 1-5)
    for sid in range(1, 6):
        lines.append(make_connection_line(sid, _ts(500 + sid * 10)))

    # 3 unexpected disconnects — sessions 50-52 have NO matching connection
    # in this analysis window, simulating a crash fault that force-terminates
    # sessions from a prior window
    for sid in range(50, 53):
        lines.append(make_disconnect_line(sid, _ts(600 + (sid - 50) * 10)))

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
    """50-player load: 50 connections, 20 normal ticks, 3 zone ticks,
    game-mechanic activity, 0 errors."""
    lines: list[str] = []

    # 50 connections
    for sid in range(1, 51):
        lines.append(make_connection_line(sid, _ts(sid)))

    # 20 normal ticks
    for i in range(1, 21):
        lines.append(make_tick_line(i, 3.0, False, _ts(100 + (i - 1) * 50)))

    # 3 zone ticks (with game-mechanic fields)
    lines.append(make_zone_tick_line(1, 20, 3.0, _ts(1200), casts_started=10, total_damage_dealt=5000, attacks_processed=8))
    lines.append(make_zone_tick_line(2, 15, 2.5, _ts(1210), casts_started=8, total_damage_dealt=3500, attacks_processed=6))
    lines.append(make_zone_tick_line(3, 15, 2.8, _ts(1220), casts_started=7, total_damage_dealt=3000, attacks_processed=5))

    # Game-mechanic events (casts + combat) so determine_status sees activity
    for i in range(10):
        sid = (i % 5) + 1
        lines.append(make_cast_started_line(sid, 100 + i, _ts(1300 + i * 20)))
    for i in range(8):
        sid = (i % 5) + 1
        lines.append(make_cast_completed_line(sid, 100 + i, _ts(1500 + i * 20)))
    for i in range(5):
        lines.append(make_damage_dealt_line((i % 3) + 1, 100, 500 + i * 100, _ts(1700 + i * 20)))

    return write_log(tmp_path, "fifty_player.jsonl", lines)


@pytest.fixture()
def baseline_game_mechanic_log(tmp_path: Path) -> Path:
    """Healthy game operation with game-mechanic telemetry.

    20 normal ticks, 3 connections, 10 cast starts, 8 completed,
    1 interrupted, 1 GCD blocked, 5 damage dealt events.
    Result: ~80% cast success, ~9% GCD block → healthy.
    """
    lines: list[str] = []

    # 20 normal ticks (3-5ms, no overrun)
    for i in range(1, 21):
        lines.append(make_tick_line(i, 3.0 + (i % 3) * 0.5, False, _ts((i - 1) * 50)))

    # 3 connections
    for sid in range(1, 4):
        lines.append(make_connection_line(sid, _ts(1000 + sid * 10)))

    # 10 cast starts across sessions 1-3
    for i in range(10):
        sid = (i % 3) + 1
        lines.append(make_cast_started_line(sid, 10 + i, _ts(1100 + i * 50)))

    # 8 cast completions (80% success)
    for i in range(8):
        sid = (i % 3) + 1
        lines.append(make_cast_completed_line(sid, 10 + i, _ts(1600 + i * 50)))

    # 1 interrupted
    lines.append(make_cast_interrupted_line(2, 18, "movement", _ts(2000)))

    # 1 GCD blocked
    lines.append(make_gcd_blocked_line(3, 19, _ts(2050)))

    # 5 damage dealt
    for i in range(5):
        lines.append(make_damage_dealt_line(
            (i % 2) + 1, 100, 200 + i * 50, _ts(2100 + i * 50),
        ))

    return write_log(tmp_path, "baseline_game_mechanic.jsonl", lines)


@pytest.fixture()
def fault_degraded_game_mechanic_log(tmp_path: Path) -> Path:
    """Latency spike degradation with game-mechanic telemetry.

    10 normal ticks + 5 spike ticks (65-85ms, overrun=true — warning level,
    below 100ms critical threshold), 3 connections, 10 cast starts,
    3 completed, 7 interrupted (reason: tick_overrun), 5 GCD blocked,
    2 damage dealt.
    Result: 30% cast success, 33% GCD block, 33% overrun → degraded.
    """
    lines: list[str] = []

    # 10 normal ticks
    for i in range(1, 11):
        lines.append(make_tick_line(i, 3.0, False, _ts((i - 1) * 50)))

    # 5 spike ticks (65-85ms, overrun — warning level, not critical)
    for i in range(5):
        lines.append(make_tick_line(
            11 + i, 65.0 + i * 5.0, True, _ts(500 + i * 50),
        ))

    # 3 connections
    for sid in range(1, 4):
        lines.append(make_connection_line(sid, _ts(800 + sid * 10)))

    # 10 cast starts
    for i in range(10):
        sid = (i % 3) + 1
        lines.append(make_cast_started_line(sid, 10 + i, _ts(900 + i * 50)))

    # 3 completions (30% success — degraded)
    for i in range(3):
        sid = (i % 3) + 1
        lines.append(make_cast_completed_line(sid, 10 + i, _ts(1400 + i * 50)))

    # 7 interrupted (reason: tick_overrun — fault-induced)
    for i in range(7):
        sid = (i % 3) + 1
        lines.append(make_cast_interrupted_line(
            sid, 13 + i, "tick_overrun", _ts(1550 + i * 50),
        ))

    # 5 GCD blocked (high GCD block rate under stress)
    for i in range(5):
        sid = (i % 3) + 1
        lines.append(make_gcd_blocked_line(sid, 20 + i, _ts(1900 + i * 50)))

    # 2 damage dealt (reduced combat activity)
    lines.append(make_damage_dealt_line(1, 100, 300, _ts(2200)))
    lines.append(make_damage_dealt_line(2, 100, 150, _ts(2250)))

    return write_log(tmp_path, "fault_degraded_game_mechanic.jsonl", lines)
