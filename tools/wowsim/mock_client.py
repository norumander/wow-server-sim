"""Mock game client spawner for stress testing and traffic simulation.

Provides async TCP clients that generate WoW-realistic game traffic
(movement, spell casts, combat) at configurable rates, plus an
orchestrator that spawns N concurrent clients.
"""

from __future__ import annotations

import random

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
