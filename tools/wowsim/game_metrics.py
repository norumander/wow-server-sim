"""Shared game-mechanic aggregation module.

Pure functions that compute cast metrics, combat metrics, and per-entity DPS
from telemetry entries. No I/O — takes lists of TelemetryEntry and returns
Pydantic models.
"""

from __future__ import annotations

from collections import defaultdict

from wowsim.models import (
    CastMetrics,
    CombatMetrics,
    EntityDPS,
    GameMechanicSummary,
    TelemetryEntry,
)


def _compute_duration_seconds(entries: list[TelemetryEntry]) -> float:
    """Compute seconds between first and last timestamps in entries."""
    if len(entries) < 2:
        return 0.0
    timestamps = [e.timestamp for e in entries]
    return (max(timestamps) - min(timestamps)).total_seconds()


def aggregate_cast_metrics(
    entries: list[TelemetryEntry],
    duration_seconds: float | None = None,
) -> CastMetrics:
    """Aggregate spell-casting statistics from spellcast telemetry events."""
    started = 0
    completed = 0
    interrupted = 0
    gcd_blocked = 0

    for e in entries:
        if e.component != "spellcast":
            continue
        if e.message == "Cast started":
            started += 1
        elif e.message == "Cast completed":
            completed += 1
        elif e.message == "Cast interrupted":
            interrupted += 1
        elif e.message == "Cast blocked by GCD":
            gcd_blocked += 1

    if duration_seconds is None:
        duration_seconds = _compute_duration_seconds(entries)

    cast_success_rate = completed / started if started > 0 else 0.0
    total_attempts = started + gcd_blocked
    gcd_block_rate = gcd_blocked / total_attempts if total_attempts > 0 else 0.0
    cast_rate_per_sec = started / duration_seconds if duration_seconds > 0 else 0.0

    return CastMetrics(
        casts_started=started,
        casts_completed=completed,
        casts_interrupted=interrupted,
        gcd_blocked=gcd_blocked,
        cast_success_rate=cast_success_rate,
        gcd_block_rate=gcd_block_rate,
        cast_rate_per_sec=cast_rate_per_sec,
    )


def compute_entity_dps(
    entries: list[TelemetryEntry],
    duration_seconds: float | None = None,
) -> list[EntityDPS]:
    """Compute per-entity damage stats, sorted descending by total_damage."""
    damage_by_entity: dict[int, int] = defaultdict(int)
    attacks_by_entity: dict[int, int] = defaultdict(int)

    for e in entries:
        if e.component == "combat" and e.message == "Damage dealt":
            attacker_id = e.data.get("attacker_id", 0)
            damage = e.data.get("actual_damage", 0)
            damage_by_entity[attacker_id] += damage
            attacks_by_entity[attacker_id] += 1

    if duration_seconds is None:
        duration_seconds = _compute_duration_seconds(entries)

    result: list[EntityDPS] = []
    for entity_id in damage_by_entity:
        total = damage_by_entity[entity_id]
        dps = total / duration_seconds if duration_seconds > 0 else 0.0
        result.append(
            EntityDPS(
                entity_id=entity_id,
                total_damage=total,
                dps=dps,
                attack_count=attacks_by_entity[entity_id],
            )
        )

    result.sort(key=lambda x: x.total_damage, reverse=True)
    return result


def aggregate_combat_metrics(
    entries: list[TelemetryEntry],
    duration_seconds: float | None = None,
) -> CombatMetrics:
    """Aggregate combat statistics from combat telemetry events."""
    total_damage = 0
    total_attacks = 0
    kills = 0
    attacker_ids: set[int] = set()

    for e in entries:
        if e.component != "combat":
            continue
        if e.message == "Damage dealt":
            total_damage += e.data.get("actual_damage", 0)
            total_attacks += 1
            attacker_ids.add(e.data.get("attacker_id", 0))
        elif e.message == "Entity killed":
            kills += 1

    if duration_seconds is None:
        duration_seconds = _compute_duration_seconds(entries)

    overall_dps = total_damage / duration_seconds if duration_seconds > 0 else 0.0

    return CombatMetrics(
        total_damage=total_damage,
        total_attacks=total_attacks,
        kills=kills,
        active_entities=len(attacker_ids),
        overall_dps=overall_dps,
    )


def aggregate_game_mechanics(
    entries: list[TelemetryEntry],
    top_n: int = 5,
) -> GameMechanicSummary:
    """Orchestrate all game-mechanic aggregations into a single summary."""
    duration = _compute_duration_seconds(entries)

    cast = aggregate_cast_metrics(entries, duration_seconds=duration)
    entity_dps = compute_entity_dps(entries, duration_seconds=duration)
    combat = aggregate_combat_metrics(entries, duration_seconds=duration)

    return GameMechanicSummary(
        cast_metrics=cast,
        combat_metrics=combat,
        top_damage_dealers=entity_dps[:top_n],
        duration_seconds=duration,
    )
