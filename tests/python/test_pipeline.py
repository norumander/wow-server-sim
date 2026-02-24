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


# ============================================================
# Group B: Build Preconditions (3 tests)
# ============================================================


class TestBuildPassesHealthy:
    """Build stage passes when server is reachable and healthy."""

    def test_passes_healthy(self) -> None:
        from wowsim.pipeline import check_build_preconditions

        result = check_build_preconditions(reachable=True, status="healthy")
        assert result.stage == "build"
        assert result.passed is True
        assert result.health_status == "healthy"


class TestBuildFailsUnreachable:
    """Build stage fails when server is unreachable."""

    def test_fails_unreachable(self) -> None:
        from wowsim.pipeline import check_build_preconditions

        result = check_build_preconditions(reachable=False, status="healthy")
        assert result.stage == "build"
        assert result.passed is False
        assert "unreachable" in result.message.lower()


class TestBuildFailsCritical:
    """Build stage fails when server status is critical."""

    def test_fails_critical(self) -> None:
        from wowsim.pipeline import check_build_preconditions

        result = check_build_preconditions(reachable=True, status="critical")
        assert result.stage == "build"
        assert result.passed is False
        assert "critical" in result.message.lower()


# ============================================================
# Group C: Validate Gate (2 tests)
# ============================================================


class TestValidatePassesReachable:
    """Validate stage passes when server is reachable."""

    def test_passes(self) -> None:
        from wowsim.pipeline import check_validate_gate

        from wowsim.models import HealthReport
        from datetime import datetime, timezone

        report = HealthReport(
            timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
            status="healthy",
            server_reachable=True,
        )
        result = check_validate_gate(report)
        assert result.stage == "validate"
        assert result.passed is True
        assert result.health_status == "healthy"


class TestValidateFailsUnreachable:
    """Validate stage fails when server is unreachable."""

    def test_fails(self) -> None:
        from wowsim.pipeline import check_validate_gate

        from wowsim.models import HealthReport
        from datetime import datetime, timezone

        report = HealthReport(
            timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
            status="healthy",
            server_reachable=False,
        )
        result = check_validate_gate(report)
        assert result.stage == "validate"
        assert result.passed is False
        assert "unreachable" in result.message.lower()


# ============================================================
# Group D: Canary Evaluation (3 tests)
# ============================================================


class TestCanaryPassesAllHealthy:
    """Canary passes when all health samples are below threshold."""

    def test_passes(self) -> None:
        from wowsim.pipeline import evaluate_canary_health

        samples = ["healthy", "healthy", "degraded", "healthy"]
        passed, message = evaluate_canary_health(samples, threshold="critical")
        assert passed is True


class TestCanaryFailsOnCritical:
    """Canary fails on first critical sample when threshold is critical."""

    def test_fails_critical(self) -> None:
        from wowsim.pipeline import evaluate_canary_health

        samples = ["healthy", "critical", "healthy"]
        passed, message = evaluate_canary_health(samples, threshold="critical")
        assert passed is False
        assert "critical" in message.lower()


class TestCanaryFailsOnDegradedThreshold:
    """Canary fails on degraded sample when threshold is degraded."""

    def test_fails_degraded(self) -> None:
        from wowsim.pipeline import evaluate_canary_health

        samples = ["healthy", "degraded", "healthy"]
        passed, message = evaluate_canary_health(samples, threshold="degraded")
        assert passed is False
        assert "degraded" in message.lower()


# ============================================================
# Group E: Rollback Action (2 tests)
# ============================================================


class TestRollbackReversesActivate:
    """Rollback of 'activate' returns 'deactivate'."""

    def test_reverse_activate(self) -> None:
        from wowsim.pipeline import determine_rollback_action
        from wowsim.models import PipelineConfig

        config = PipelineConfig(fault_id="latency-spike", action="activate")
        action, fault_id = determine_rollback_action(config)
        assert action == "deactivate"
        assert fault_id == "latency-spike"


class TestRollbackReversesDeactivate:
    """Rollback of 'deactivate' returns 'activate'."""

    def test_reverse_deactivate(self) -> None:
        from wowsim.pipeline import determine_rollback_action
        from wowsim.models import PipelineConfig

        config = PipelineConfig(fault_id="memory-pressure", action="deactivate")
        action, fault_id = determine_rollback_action(config)
        assert action == "activate"
        assert fault_id == "memory-pressure"
