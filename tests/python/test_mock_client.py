"""Tests for the mock client spawner (wowsim.mock_client).

Groups:
  A — Mock Client Models (3 tests)
  B — Traffic Generation (3 tests)
  C — Action Selection (2 tests)
  D — Single Client Lifecycle (3 tests)
  E — Multi-Client Spawning (3 tests)
  F — Formatting (2 tests)
  G — CLI Integration (2 tests)
"""

from __future__ import annotations

from wowsim.models import ClientConfig, ClientResult, SpawnResult


# ---------------------------------------------------------------------------
# Group A: Mock Client Models
# ---------------------------------------------------------------------------


class TestClientModels:
    """Verify Pydantic models for mock client configuration and results."""

    def test_client_config_defaults(self) -> None:
        cfg = ClientConfig()
        assert cfg.host == "localhost"
        assert cfg.port == 8080
        assert cfg.actions_per_second == 2.0
        assert cfg.duration_seconds == 10.0

    def test_client_result_round_trip(self) -> None:
        result = ClientResult(
            client_id=0,
            connected=True,
            actions_sent=10,
            duration_seconds=5.0,
            error=None,
        )
        json_str = result.model_dump_json()
        restored = ClientResult.model_validate_json(json_str)
        assert restored == result

    def test_spawn_result_aggregation(self) -> None:
        clients = [
            ClientResult(
                client_id=0, connected=True, actions_sent=10, duration_seconds=5.0
            ),
            ClientResult(
                client_id=1, connected=True, actions_sent=8, duration_seconds=5.0
            ),
            ClientResult(
                client_id=2,
                connected=False,
                actions_sent=0,
                duration_seconds=0.0,
                error="Connection refused",
            ),
        ]
        result = SpawnResult(
            total_clients=3,
            successful_connections=2,
            failed_connections=1,
            total_actions_sent=18,
            total_duration_seconds=5.0,
            clients=clients,
        )
        assert result.total_clients == 3
        assert result.successful_connections == 2
        assert result.failed_connections == 1
        assert result.total_actions_sent == 18
        assert len(result.clients) == 3


# ---------------------------------------------------------------------------
# Group B: Traffic Generation
# ---------------------------------------------------------------------------

from wowsim.mock_client import (
    CAST_TIMES,
    SPELL_IDS,
    generate_combat_action,
    generate_movement_action,
    generate_spell_cast_action,
)


class TestTrafficGeneration:
    """Verify pure traffic generation functions produce valid payloads."""

    def test_generate_movement_action(self) -> None:
        action = generate_movement_action(client_id=1, x=10.0, y=20.0, z=0.0)
        assert action["type"] == "movement"
        assert action["session_id"] == 1
        pos = action["position"]
        assert abs(pos["x"] - 10.0) <= 5.0
        assert abs(pos["y"] - 20.0) <= 5.0
        assert abs(pos["z"] - 0.0) <= 0.5

    def test_generate_spell_cast_action(self) -> None:
        action = generate_spell_cast_action(client_id=2)
        assert action["type"] == "spell_cast"
        assert action["session_id"] == 2
        assert action["action"] == "CAST_START"
        assert action["spell_id"] in SPELL_IDS
        assert action["cast_time_ticks"] in CAST_TIMES

    def test_generate_combat_action(self) -> None:
        action = generate_combat_action(client_id=3, target_id=1000001)
        assert action["type"] == "combat"
        assert action["session_id"] == 3
        assert action["action"] == "ATTACK"
        assert action["target_session_id"] == 1000001
        assert 10 <= action["base_damage"] <= 50
        assert action["damage_type"] in ("PHYSICAL", "MAGICAL")


# ---------------------------------------------------------------------------
# Group C: Action Selection
# ---------------------------------------------------------------------------

from wowsim.mock_client import choose_action


class TestActionSelection:
    """Verify weighted random action selection."""

    def test_choose_action_valid_types(self) -> None:
        types = set()
        for _ in range(100):
            action = choose_action(client_id=0, x=0.0, y=0.0, z=0.0)
            types.add(action["type"])
        assert types == {"movement", "spell_cast", "combat"}

    def test_choose_action_weight_distribution(self) -> None:
        counts: dict[str, int] = {"movement": 0, "spell_cast": 0, "combat": 0}
        n = 1000
        for _ in range(n):
            action = choose_action(client_id=0, x=0.0, y=0.0, z=0.0)
            counts[action["type"]] += 1

        # Expected: movement ~50%, spell_cast ~30%, combat ~20% (±15%)
        assert abs(counts["movement"] / n - 0.50) < 0.15
        assert abs(counts["spell_cast"] / n - 0.30) < 0.15
        assert abs(counts["combat"] / n - 0.20) < 0.15


# ---------------------------------------------------------------------------
# Group D: Single Client Lifecycle
# ---------------------------------------------------------------------------

import asyncio
import time

from wowsim.mock_client import MockGameClient


class TestSingleClientLifecycle:
    """Verify individual mock client connect, send, and run loop."""

    def test_mock_client_connects(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        server = mock_game_server["server"]

        async def _test() -> None:
            async with MockGameClient(client_id=0, host=host, port=port) as client:
                assert client.connected
            # Give server time to register the connection
            await asyncio.sleep(0.05)

        asyncio.run(_test())
        assert server.connection_count >= 1

    def test_mock_client_sends_actions(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        server = mock_game_server["server"]

        async def _test() -> None:
            async with MockGameClient(client_id=0, host=host, port=port) as client:
                await client.send_action()
                await client.send_action()
                await asyncio.sleep(0.05)

        asyncio.run(_test())
        assert server.bytes_received > 0

    def test_mock_client_run_loop(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        cfg = ClientConfig(
            host=host, port=port, actions_per_second=20.0, duration_seconds=0.5
        )

        async def _test() -> ClientResult:
            client = MockGameClient(client_id=0, host=cfg.host, port=cfg.port)
            return await client.run(cfg)

        result = asyncio.run(_test())
        assert result.connected is True
        assert result.actions_sent >= 1
        assert result.error is None


# ---------------------------------------------------------------------------
# Group E: Multi-Client Spawning
# ---------------------------------------------------------------------------

from wowsim.mock_client import spawn_clients


class TestMultiClientSpawning:
    """Verify concurrent client spawning and failure handling."""

    def test_spawn_clients_all_connect(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        cfg = ClientConfig(
            host=host, port=port, actions_per_second=10.0, duration_seconds=0.5
        )
        result = asyncio.run(spawn_clients(cfg, count=5))
        assert result.total_clients == 5
        assert result.successful_connections == 5
        assert result.failed_connections == 0

    def test_spawn_clients_connection_failure(self) -> None:
        cfg = ClientConfig(
            host="127.0.0.1", port=1, actions_per_second=1.0, duration_seconds=0.5
        )
        result = asyncio.run(spawn_clients(cfg, count=3))
        assert result.total_clients == 3
        assert result.failed_connections == 3
        assert result.successful_connections == 0
        for client in result.clients:
            assert client.error is not None

    def test_spawn_clients_server_receives_data(self, mock_game_server: dict) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        server = mock_game_server["server"]
        cfg = ClientConfig(
            host=host, port=port, actions_per_second=20.0, duration_seconds=0.5
        )
        asyncio.run(spawn_clients(cfg, count=10))
        assert server.bytes_received > 0
