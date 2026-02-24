"""Tests for wowsim.pipeline — hotfix deployment pipeline."""

from __future__ import annotations

import json

import pytest


# ============================================================
# Group A: Pipeline Models (3 tests)
# ============================================================


class TestPipelineConfigDefaults:
    """PipelineConfig provides sensible defaults for optional fields."""

    def test_defaults(self) -> None:
        from wowsim.models import PipelineConfig

        config = PipelineConfig(fault_id="latency-spike", action="activate")
        assert config.version == "1.0.0"
        assert config.fault_id == "latency-spike"
        assert config.action == "activate"
        assert config.params == {}
        assert config.target_zone_id == 0
        assert config.duration_ticks == 0
        assert config.canary_duration_seconds == 10.0
        assert config.canary_poll_interval_seconds == 2.0
        assert config.rollback_on == "critical"
        assert config.game_host == "localhost"
        assert config.game_port == 8080
        assert config.control_host == "localhost"
        assert config.control_port == 8081
        assert config.log_file is None


class TestPipelineConfigCustomOverrides:
    """PipelineConfig accepts custom values for all fields."""

    def test_overrides(self) -> None:
        from wowsim.models import PipelineConfig

        config = PipelineConfig(
            fault_id="memory-pressure",
            action="deactivate",
            version="2.0.0",
            params={"megabytes": 256},
            target_zone_id=1,
            duration_ticks=100,
            canary_duration_seconds=30.0,
            canary_poll_interval_seconds=5.0,
            rollback_on="degraded",
            game_host="10.0.0.1",
            game_port=9090,
            control_host="10.0.0.2",
            control_port=9091,
            log_file="telemetry.jsonl",
        )
        assert config.fault_id == "memory-pressure"
        assert config.action == "deactivate"
        assert config.version == "2.0.0"
        assert config.params == {"megabytes": 256}
        assert config.target_zone_id == 1
        assert config.duration_ticks == 100
        assert config.canary_duration_seconds == 30.0
        assert config.canary_poll_interval_seconds == 5.0
        assert config.rollback_on == "degraded"
        assert config.game_host == "10.0.0.1"
        assert config.game_port == 9090
        assert config.control_host == "10.0.0.2"
        assert config.control_port == 9091
        assert config.log_file == "telemetry.jsonl"


class TestPipelineModelsJsonRoundTrip:
    """PipelineResult serializes to JSON and deserializes back."""

    def test_round_trip(self) -> None:
        from wowsim.models import PipelineConfig, PipelineResult, StageResult

        config = PipelineConfig(fault_id="latency-spike", action="activate")
        stages = [
            StageResult(
                stage="build",
                passed=True,
                message="Server reachable",
                duration_seconds=0.1,
                health_status="healthy",
            ),
            StageResult(
                stage="validate",
                passed=True,
                message="Validation passed",
                duration_seconds=0.2,
            ),
        ]
        result = PipelineResult(
            config=config,
            stages=stages,
            outcome="promoted",
            total_duration_seconds=1.5,
        )
        json_str = result.model_dump_json()
        restored = PipelineResult.model_validate_json(json_str)
        assert restored.outcome == "promoted"
        assert len(restored.stages) == 2
        assert restored.stages[0].stage == "build"
        assert restored.stages[0].passed is True
        assert restored.stages[1].stage == "validate"
        assert restored.config.fault_id == "latency-spike"
        assert restored.total_duration_seconds == 1.5
