"""Integration tests for client connection lifecycle.

Verifies that mock clients compose correctly with telemetry parsing
and health check tools in realistic connection workflows.

Tests:
  1 — Clients connect to mock server
  2 — Connection telemetry parsed correctly
  3 — Player count from telemetry
  4 — Server reachable with mock
  5 — Client spawn with telemetry reconciliation
  6 — Fifty clients all connect
"""

from __future__ import annotations

import json
from pathlib import Path

from wowsim.health_check import check_server_reachable, estimate_player_count
from wowsim.log_parser import filter_entries, parse_file
from wowsim.mock_client import run_spawn
from wowsim.models import ClientConfig


class TestClientsConnectToMockServer:
    """run_spawn with 5 clients -> all connect, server receives data."""

    def test_five_clients_connect(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        cfg = ClientConfig(
            host=host, port=port, actions_per_second=10.0, duration_seconds=0.5
        )
        result = run_spawn(cfg, 5)
        assert result.successful_connections == 5
        assert mock_game_server["server"].bytes_received > 0


class TestConnectionTelemetryParsedCorrectly:
    """parse_file + filter_entries finds 5 connections and 2 disconnects."""

    def test_connection_events(self, normal_operation_log: Path) -> None:
        entries = parse_file(normal_operation_log)
        connections = filter_entries(
            entries, type_filter="event", message_filter="Connection accepted"
        )
        disconnects = filter_entries(
            entries, type_filter="event", message_filter="Client disconnected"
        )
        assert len(connections) == 5
        assert len(disconnects) == 2


class TestPlayerCountFromTelemetry:
    """parse_file + estimate_player_count -> 3 (5 connected - 2 disconnected)."""

    def test_player_count(self, normal_operation_log: Path) -> None:
        entries = parse_file(normal_operation_log)
        count = estimate_player_count(entries)
        assert count == 3


class TestServerReachableWithMock:
    """check_server_reachable returns True for running server, False otherwise."""

    def test_reachable_and_unreachable(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        assert check_server_reachable(host, port) is True

        # Verify unreachable against a known-closed port
        assert check_server_reachable("127.0.0.1", 1, timeout=0.5) is False


class TestClientSpawnWithTelemetryReconciliation:
    """Spawn 8 clients, write matching telemetry, verify count consistency."""

    def test_spawn_and_telemetry_match(
        self, mock_game_server: dict, tmp_path: Path
    ) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        cfg = ClientConfig(
            host=host, port=port, actions_per_second=10.0, duration_seconds=0.5
        )
        spawn_result = run_spawn(cfg, 8)
        assert spawn_result.successful_connections == 8

        # Synthesize matching telemetry (8 connection events, 0 disconnects)
        lines = []
        for i in range(8):
            line = json.dumps({
                "v": 1,
                "timestamp": f"2026-02-24T12:00:00.{i:03d}Z",
                "type": "event",
                "component": "game_server",
                "message": "Connection accepted",
                "data": {"session_id": i},
            })
            lines.append(line)
        log_path = tmp_path / "reconciliation.jsonl"
        log_path.write_text("\n".join(lines) + "\n")

        entries = parse_file(log_path)
        player_count = estimate_player_count(entries)
        assert player_count == spawn_result.successful_connections


class TestFiftyClientsAllConnect:
    """50 concurrent clients all connect successfully (PRD criterion 1)."""

    def test_fifty_clients(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        cfg = ClientConfig(
            host=host, port=port, actions_per_second=10.0, duration_seconds=0.5
        )
        result = run_spawn(cfg, 50)
        assert result.successful_connections == 50
