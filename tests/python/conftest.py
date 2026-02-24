"""Shared fixtures for wowsim Python tests."""

import json
import socketserver
import threading
from datetime import datetime, timezone
from pathlib import Path

import pytest

from wowsim.models import TelemetryEntry


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


# --- Mock TCP control channel server ---

# Default canned responses keyed by command type.
_DEFAULT_CONTROL_RESPONSES: dict[str, dict] = {
    "activate": {
        "success": True,
        "command": "activate",
        "fault_id": "latency-spike",
    },
    "deactivate": {
        "success": True,
        "command": "deactivate",
        "fault_id": "latency-spike",
    },
    "deactivate_all": {
        "success": True,
        "command": "deactivate_all",
    },
    "status": {
        "success": True,
        "command": "status",
        "fault_id": "latency-spike",
        "status": {
            "id": "latency-spike",
            "mode": "tick_scoped",
            "active": True,
            "activations": 1,
            "ticks_elapsed": 42,
            "config": {"delay_ms": 200},
        },
    },
    "list": {
        "success": True,
        "command": "list",
        "faults": [
            {"id": "latency-spike", "mode": "tick_scoped", "active": False},
            {"id": "session-crash", "mode": "tick_scoped", "active": False},
            {"id": "event-queue-flood", "mode": "tick_scoped", "active": False},
            {"id": "memory-pressure", "mode": "ambient", "active": False},
        ],
    },
}


class _MockControlHandler(socketserver.StreamRequestHandler):
    """Handles one control channel client connection."""

    def handle(self) -> None:
        for raw_line in self.rfile:
            line = raw_line.decode().strip()
            if not line:
                continue
            try:
                request = json.loads(line)
            except json.JSONDecodeError:
                resp = {"success": False, "error": "Invalid JSON"}
                self.wfile.write((json.dumps(resp) + "\n").encode())
                continue

            self.server.received.append(request)  # type: ignore[attr-defined]

            cmd = request.get("command", "")
            responses = self.server.responses  # type: ignore[attr-defined]
            if cmd in responses:
                resp = dict(responses[cmd])
                # Echo back fault_id from request when present.
                if "fault_id" in request and "fault_id" in resp:
                    resp["fault_id"] = request["fault_id"]
            else:
                resp = {"success": False, "error": f"Unknown command: {cmd}"}

            self.wfile.write((json.dumps(resp) + "\n").encode())
            self.wfile.flush()


class _MockControlServer(socketserver.TCPServer):
    allow_reuse_address = True

    def __init__(self, responses: dict[str, dict] | None = None) -> None:
        self.received: list[dict] = []
        self.responses = dict(responses or _DEFAULT_CONTROL_RESPONSES)
        super().__init__(("127.0.0.1", 0), _MockControlHandler)


@pytest.fixture()
def mock_control_server():
    """TCP server on an ephemeral port that mimics the control channel.

    Yields a dict with:
        host:      "127.0.0.1"
        port:      OS-assigned ephemeral port
        received:  list of all received JSON request dicts
        responses: mutable dict of canned responses keyed by command
        server:    the underlying TCPServer (for overriding responses)
    """
    server = _MockControlServer()
    host, port = server.server_address
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield {
            "host": host,
            "port": port,
            "received": server.received,
            "responses": server.responses,
            "server": server,
        }
    finally:
        server.shutdown()
        server.server_close()


# --- Health check fixtures ---

HEALTH_BASE_TS = "2026-02-24T10:00:00"


@pytest.fixture()
def health_log_entries() -> list[TelemetryEntry]:
    """Parsed telemetry entries for health computation tests.

    Includes: 5 game_loop ticks (1 overrun), 2 zone tick metrics,
    1 zone error, 2 connections, 1 disconnection.
    """
    lines = [
        # 5 game_loop tick metrics (tick 3 has overrun)
        _make_line(
            "metric", "game_loop", "Tick completed",
            {"tick": 1, "duration_ms": 3.0, "overrun": False},
            f"{HEALTH_BASE_TS}.000Z",
        ),
        _make_line(
            "metric", "game_loop", "Tick completed",
            {"tick": 2, "duration_ms": 4.0, "overrun": False},
            f"{HEALTH_BASE_TS}.050Z",
        ),
        _make_line(
            "metric", "game_loop", "Tick completed",
            {"tick": 3, "duration_ms": 60.0, "overrun": True},
            f"{HEALTH_BASE_TS}.100Z",
        ),
        _make_line(
            "metric", "game_loop", "Tick completed",
            {"tick": 4, "duration_ms": 3.5, "overrun": False},
            f"{HEALTH_BASE_TS}.150Z",
        ),
        _make_line(
            "metric", "game_loop", "Tick completed",
            {"tick": 5, "duration_ms": 4.5, "overrun": False},
            f"{HEALTH_BASE_TS}.200Z",
        ),
        # 2 zone tick metrics
        _make_line(
            "metric", "zone", "Zone tick completed",
            {"zone_id": 1, "events_processed": 5, "duration_ms": 3.2},
            f"{HEALTH_BASE_TS}.010Z",
        ),
        _make_line(
            "metric", "zone", "Zone tick completed",
            {"zone_id": 2, "events_processed": 3, "duration_ms": 2.8},
            f"{HEALTH_BASE_TS}.020Z",
        ),
        # 1 zone error
        _make_line(
            "error", "zone", "Zone tick exception",
            {"zone_id": 1, "error": "null pointer"},
            f"{HEALTH_BASE_TS}.110Z",
        ),
        # 2 connections, 1 disconnection
        _make_line(
            "event", "game_server", "Connection accepted",
            {"session_id": 1},
            f"{HEALTH_BASE_TS}.001Z",
        ),
        _make_line(
            "event", "game_server", "Connection accepted",
            {"session_id": 2},
            f"{HEALTH_BASE_TS}.002Z",
        ),
        _make_line(
            "event", "game_server", "Client disconnected",
            {"session_id": 1},
            f"{HEALTH_BASE_TS}.250Z",
        ),
    ]
    from wowsim.log_parser import parse_line

    entries = [parse_line(line) for line in lines]
    return [e for e in entries if e is not None]


@pytest.fixture()
def health_log_file(tmp_path: Path, health_log_entries: list[TelemetryEntry]) -> Path:
    """Temp JSONL file containing health_log_entries."""
    path = tmp_path / "health_telemetry.jsonl"
    lines = [e.model_dump_json() for e in health_log_entries]
    path.write_text("\n".join(lines) + "\n")
    return path


# --- Mock game server (for mock client tests) ---


class _MockGameHandler(socketserver.StreamRequestHandler):
    """Accepts connections and discards data (mirrors C++ server behavior)."""

    def handle(self) -> None:
        self.server.connection_count += 1  # type: ignore[attr-defined]
        try:
            while True:
                data = self.rfile.read(4096)
                if not data:
                    break
                self.server.bytes_received += len(data)  # type: ignore[attr-defined]
        except Exception:
            pass
        finally:
            self.server.disconnection_count += 1  # type: ignore[attr-defined]


class _MockGameServer(socketserver.ThreadingTCPServer):
    """Threading TCP server that accepts and discards data like the C++ server."""

    allow_reuse_address = True

    def __init__(self) -> None:
        self.connection_count: int = 0
        self.disconnection_count: int = 0
        self.bytes_received: int = 0
        super().__init__(("127.0.0.1", 0), _MockGameHandler)


@pytest.fixture()
def mock_game_server():
    """TCP server on an ephemeral port that mimics the game server.

    Yields a dict with:
        host:   "127.0.0.1"
        port:   OS-assigned ephemeral port
        server: the underlying ThreadingTCPServer
    """
    server = _MockGameServer()
    host, port = server.server_address
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield {
            "host": host,
            "port": port,
            "server": server,
        }
    finally:
        server.shutdown()
        server.server_close()
