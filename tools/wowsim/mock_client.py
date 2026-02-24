"""Mock game client spawner for stress testing and traffic simulation.

Provides async TCP clients that generate WoW-realistic game traffic
(movement, spell casts, combat) at configurable rates, plus an
orchestrator that spawns N concurrent clients.
"""

from __future__ import annotations

import asyncio
import json
import random
import time

from wowsim.models import ClientConfig, ClientResult, SpawnResult

# ---------------------------------------------------------------------------
# WoW-realistic traffic pattern weights
# ---------------------------------------------------------------------------

ACTION_WEIGHTS: dict[str, float] = {
    "movement": 0.50,
    "spell_cast": 0.30,
    "combat": 0.20,
}
"""Probability weights for each action type (must sum to 1.0)."""

SPELL_IDS: list[int] = [100, 101, 102, 103, 200, 201, 300]
"""Available spell IDs matching C++ SpellCastEvent conventions."""

CAST_TIMES: list[int] = [0, 0, 20, 30, 40]
"""Cast time options in ticks (0 = instant, weighted towards instant)."""

# Target IDs for combat actions (NPC convention: >= 1,000,000)
_DEFAULT_TARGET_ID: int = 1_000_001


# ---------------------------------------------------------------------------
# Traffic generation (pure functions, no I/O)
# ---------------------------------------------------------------------------


def generate_movement_action(
    client_id: int, x: float, y: float, z: float
) -> dict:
    """Generate a movement event payload with small random delta.

    Position offsets: ±5 for x/y, ±0.5 for z.
    """
    return {
        "type": "movement",
        "session_id": client_id,
        "position": {
            "x": x + random.uniform(-5.0, 5.0),
            "y": y + random.uniform(-5.0, 5.0),
            "z": z + random.uniform(-0.5, 0.5),
        },
    }


def generate_spell_cast_action(client_id: int) -> dict:
    """Generate a spell cast event with random spell and cast time."""
    return {
        "type": "spell_cast",
        "session_id": client_id,
        "action": "CAST_START",
        "spell_id": random.choice(SPELL_IDS),
        "cast_time_ticks": random.choice(CAST_TIMES),
    }


def generate_combat_action(client_id: int, target_id: int) -> dict:
    """Generate a combat attack event with random damage."""
    return {
        "type": "combat",
        "session_id": client_id,
        "action": "ATTACK",
        "target_session_id": target_id,
        "base_damage": random.randint(10, 50),
        "damage_type": random.choice(["PHYSICAL", "MAGICAL"]),
    }


def choose_action(client_id: int, x: float, y: float, z: float) -> dict:
    """Select a random action using weighted distribution.

    Returns a dict payload matching C++ event types.
    """
    action_type = random.choices(
        list(ACTION_WEIGHTS.keys()),
        weights=list(ACTION_WEIGHTS.values()),
        k=1,
    )[0]

    if action_type == "movement":
        return generate_movement_action(client_id, x, y, z)
    elif action_type == "spell_cast":
        return generate_spell_cast_action(client_id)
    else:
        return generate_combat_action(client_id, _DEFAULT_TARGET_ID)


# ---------------------------------------------------------------------------
# Async TCP client
# ---------------------------------------------------------------------------


class MockGameClient:
    """Async TCP client that generates WoW-realistic game traffic.

    Usage::

        async with MockGameClient(client_id=0, host="localhost", port=8080) as c:
            result = await c.run(config)
    """

    def __init__(self, client_id: int, host: str, port: int) -> None:
        self._client_id = client_id
        self._host = host
        self._port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._x: float = 0.0
        self._y: float = 0.0
        self._z: float = 0.0
        self._actions_sent: int = 0

    @property
    def connected(self) -> bool:
        """Whether the client has an active connection."""
        return self._writer is not None

    async def connect(self) -> None:
        """Open TCP connection to the game server."""
        self._reader, self._writer = await asyncio.open_connection(
            self._host, self._port
        )

    async def close(self) -> None:
        """Close the TCP connection."""
        if self._writer is not None:
            self._writer.close()
            await self._writer.wait_closed()
            self._writer = None
            self._reader = None

    async def __aenter__(self) -> MockGameClient:
        await self.connect()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.close()

    async def send_action(self) -> None:
        """Generate and send a single random action to the server."""
        if self._writer is None:
            return
        action = choose_action(self._client_id, self._x, self._y, self._z)
        # Update position tracking for movement actions
        if action["type"] == "movement":
            self._x = action["position"]["x"]
            self._y = action["position"]["y"]
            self._z = action["position"]["z"]
        payload = json.dumps(action) + "\n"
        self._writer.write(payload.encode())
        await self._writer.drain()
        self._actions_sent += 1

    async def run(self, config: ClientConfig) -> ClientResult:
        """Run the client loop for the configured duration.

        Connects (if not already connected), sends actions at the
        configured rate, then disconnects. Returns a ClientResult.
        """
        start = time.monotonic()
        try:
            if not self.connected:
                await self.connect()
            interval = 1.0 / config.actions_per_second
            deadline = start + config.duration_seconds
            while time.monotonic() < deadline:
                await self.send_action()
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    await asyncio.sleep(min(interval, remaining))
        except OSError as exc:
            return ClientResult(
                client_id=self._client_id,
                connected=False,
                actions_sent=self._actions_sent,
                duration_seconds=time.monotonic() - start,
                error=str(exc),
            )
        finally:
            await self.close()

        return ClientResult(
            client_id=self._client_id,
            connected=True,
            actions_sent=self._actions_sent,
            duration_seconds=time.monotonic() - start,
        )


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------


async def _run_one(client_id: int, config: ClientConfig) -> ClientResult:
    """Run a single mock client, capturing all failures."""
    client = MockGameClient(client_id=client_id, host=config.host, port=config.port)
    try:
        return await client.run(config)
    except Exception as exc:
        return ClientResult(
            client_id=client_id,
            connected=False,
            actions_sent=0,
            duration_seconds=0.0,
            error=str(exc),
        )


async def spawn_clients(config: ClientConfig, count: int) -> SpawnResult:
    """Spawn N concurrent mock clients and collect results.

    Each client runs independently via asyncio.gather, connecting to
    the server specified in config and generating traffic for the
    configured duration.
    """
    start = time.monotonic()
    tasks = [_run_one(i, config) for i in range(count)]
    results = await asyncio.gather(*tasks)
    elapsed = time.monotonic() - start

    successful = sum(1 for r in results if r.connected)
    total_actions = sum(r.actions_sent for r in results)

    return SpawnResult(
        total_clients=count,
        successful_connections=successful,
        failed_connections=count - successful,
        total_actions_sent=total_actions,
        total_duration_seconds=round(elapsed, 2),
        clients=list(results),
    )


def run_spawn(config: ClientConfig, count: int) -> SpawnResult:
    """Synchronous wrapper around spawn_clients for CLI use."""
    return asyncio.run(spawn_clients(config, count))
