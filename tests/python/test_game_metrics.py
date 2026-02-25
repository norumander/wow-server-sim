"""Tests for game-mechanic Pydantic models and aggregation functions."""

from __future__ import annotations

import pytest

from wowsim.models import TelemetryEntry


# ============================================================
# Group A: Game-Mechanic Pydantic Models (4 tests)
# ============================================================


class TestCastMetricsModel:
    """CastMetrics holds cast rate, success rate, and GCD block rate."""

    def test_fields_populated(self) -> None:
        from wowsim.models import CastMetrics

        cm = CastMetrics(
            casts_started=10,
            casts_completed=8,
            casts_interrupted=2,
            gcd_blocked=3,
            cast_success_rate=0.8,
            gcd_block_rate=0.23,
            cast_rate_per_sec=1.0,
        )
        assert cm.casts_started == 10
        assert cm.casts_completed == 8
        assert cm.casts_interrupted == 2
        assert cm.gcd_blocked == 3
        assert cm.cast_success_rate == pytest.approx(0.8)
        assert cm.gcd_block_rate == pytest.approx(0.23)
        assert cm.cast_rate_per_sec == pytest.approx(1.0)


class TestEntityDPSModel:
    """EntityDPS holds per-entity damage output stats."""

    def test_fields_populated(self) -> None:
        from wowsim.models import EntityDPS

        edps = EntityDPS(
            entity_id=1,
            total_damage=5000,
            dps=250.0,
            attack_count=10,
        )
        assert edps.entity_id == 1
        assert edps.total_damage == 5000
        assert edps.dps == pytest.approx(250.0)
        assert edps.attack_count == 10


class TestCombatMetricsModel:
    """CombatMetrics holds aggregate combat stats."""

    def test_fields_populated(self) -> None:
        from wowsim.models import CombatMetrics

        cm = CombatMetrics(
            total_damage=10000,
            total_attacks=20,
            kills=2,
            active_entities=5,
            overall_dps=500.0,
        )
        assert cm.total_damage == 10000
        assert cm.total_attacks == 20
        assert cm.kills == 2
        assert cm.active_entities == 5
        assert cm.overall_dps == pytest.approx(500.0)


class TestGameMechanicSummaryModel:
    """GameMechanicSummary composes cast, combat, and top DPS."""

    def test_fields_populated(self) -> None:
        from wowsim.models import (
            CastMetrics,
            CombatMetrics,
            EntityDPS,
            GameMechanicSummary,
        )

        summary = GameMechanicSummary(
            cast_metrics=CastMetrics(
                casts_started=10,
                casts_completed=8,
                casts_interrupted=2,
                gcd_blocked=0,
                cast_success_rate=0.8,
                gcd_block_rate=0.0,
                cast_rate_per_sec=1.0,
            ),
            combat_metrics=CombatMetrics(
                total_damage=10000,
                total_attacks=20,
                kills=2,
                active_entities=5,
                overall_dps=500.0,
            ),
            top_damage_dealers=[
                EntityDPS(entity_id=1, total_damage=5000, dps=250.0, attack_count=10),
            ],
            duration_seconds=20.0,
        )
        assert summary.cast_metrics.casts_started == 10
        assert summary.combat_metrics.total_damage == 10000
        assert len(summary.top_damage_dealers) == 1
        assert summary.duration_seconds == 20.0

    def test_json_round_trip(self) -> None:
        from wowsim.models import (
            CastMetrics,
            CombatMetrics,
            EntityDPS,
            GameMechanicSummary,
        )

        summary = GameMechanicSummary(
            cast_metrics=CastMetrics(
                casts_started=3,
                casts_completed=2,
                casts_interrupted=1,
                gcd_blocked=1,
                cast_success_rate=0.667,
                gcd_block_rate=0.25,
                cast_rate_per_sec=0.5,
            ),
            combat_metrics=CombatMetrics(
                total_damage=1000,
                total_attacks=3,
                kills=1,
                active_entities=2,
                overall_dps=100.0,
            ),
            top_damage_dealers=[
                EntityDPS(entity_id=1, total_damage=800, dps=80.0, attack_count=2),
            ],
            duration_seconds=10.0,
        )
        json_str = summary.model_dump_json()
        restored = GameMechanicSummary.model_validate_json(json_str)
        assert restored.cast_metrics.casts_started == 3
        assert restored.combat_metrics.kills == 1
        assert len(restored.top_damage_dealers) == 1


# ============================================================
# Group B: Game Metrics Aggregation Functions (7 tests)
# ============================================================


class TestAggregateCastMetrics:
    """aggregate_cast_metrics computes cast rates from telemetry entries."""

    def test_basic_aggregation(self, game_mechanic_entries: list[TelemetryEntry]) -> None:
        from wowsim.game_metrics import aggregate_cast_metrics

        result = aggregate_cast_metrics(game_mechanic_entries)
        assert result.casts_started == 3
        assert result.casts_completed == 2
        assert result.casts_interrupted == 1
        assert result.gcd_blocked == 2
        # success rate = completed / started = 2/3
        assert result.cast_success_rate == pytest.approx(2 / 3, rel=1e-3)
        # block rate = gcd_blocked / (started + gcd_blocked) = 2/5
        assert result.gcd_block_rate == pytest.approx(2 / 5, rel=1e-3)

    def test_empty_entries(self) -> None:
        from wowsim.game_metrics import aggregate_cast_metrics

        result = aggregate_cast_metrics([])
        assert result.casts_started == 0
        assert result.cast_success_rate == 0.0
        assert result.gcd_block_rate == 0.0
        assert result.cast_rate_per_sec == 0.0

    def test_with_explicit_duration(self, game_mechanic_entries: list[TelemetryEntry]) -> None:
        from wowsim.game_metrics import aggregate_cast_metrics

        result = aggregate_cast_metrics(game_mechanic_entries, duration_seconds=10.0)
        # 3 started / 10s = 0.3
        assert result.cast_rate_per_sec == pytest.approx(0.3, rel=1e-3)


class TestComputeEntityDPS:
    """compute_entity_dps computes per-entity damage stats."""

    def test_entity_dps_sorted(self, game_mechanic_entries: list[TelemetryEntry]) -> None:
        from wowsim.game_metrics import compute_entity_dps

        result = compute_entity_dps(game_mechanic_entries)
        assert len(result) == 2
        # Entity 1 did 800 damage, entity 2 did 200 — sorted desc
        assert result[0].entity_id == 1
        assert result[0].total_damage == 800
        assert result[0].attack_count == 2
        assert result[1].entity_id == 2
        assert result[1].total_damage == 200
        assert result[1].attack_count == 1

    def test_empty_entries(self) -> None:
        from wowsim.game_metrics import compute_entity_dps

        result = compute_entity_dps([])
        assert result == []


class TestAggregateCombatMetrics:
    """aggregate_combat_metrics computes global combat stats."""

    def test_basic_aggregation(self, game_mechanic_entries: list[TelemetryEntry]) -> None:
        from wowsim.game_metrics import aggregate_combat_metrics

        result = aggregate_combat_metrics(game_mechanic_entries)
        assert result.total_damage == 1000
        assert result.total_attacks == 3
        assert result.kills == 1
        assert result.active_entities == 2  # attacker IDs 1 and 2

    def test_empty_entries(self) -> None:
        from wowsim.game_metrics import aggregate_combat_metrics

        result = aggregate_combat_metrics([])
        assert result.total_damage == 0
        assert result.total_attacks == 0
        assert result.kills == 0
        assert result.active_entities == 0
        assert result.overall_dps == 0.0


class TestAggregateGameMechanics:
    """aggregate_game_mechanics orchestrates all sub-aggregations."""

    def test_full_aggregation(self, game_mechanic_entries: list[TelemetryEntry]) -> None:
        from wowsim.game_metrics import aggregate_game_mechanics

        result = aggregate_game_mechanics(game_mechanic_entries, top_n=5)
        assert result.cast_metrics.casts_started == 3
        assert result.combat_metrics.total_damage == 1000
        assert len(result.top_damage_dealers) == 2
        assert result.duration_seconds > 0

    def test_empty_entries(self) -> None:
        from wowsim.game_metrics import aggregate_game_mechanics

        result = aggregate_game_mechanics([])
        assert result.cast_metrics.casts_started == 0
        assert result.combat_metrics.total_damage == 0
        assert result.top_damage_dealers == []
        assert result.duration_seconds == 0.0

    def test_top_n_limits_output(self, game_mechanic_entries: list[TelemetryEntry]) -> None:
        from wowsim.game_metrics import aggregate_game_mechanics

        result = aggregate_game_mechanics(game_mechanic_entries, top_n=1)
        assert len(result.top_damage_dealers) == 1
        assert result.top_damage_dealers[0].entity_id == 1  # highest damage
