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


# ============================================================
# Group F: Formatting (2 tests)
# ============================================================


class TestFormatStageResult:
    """format_stage_result produces a one-line summary."""

    def test_format(self) -> None:
        from wowsim.pipeline import format_stage_result
        from wowsim.models import StageResult

        result = StageResult(
            stage="build",
            passed=True,
            message="Build preconditions met",
            duration_seconds=0.05,
            health_status="healthy",
        )
        text = format_stage_result(result)
        assert "build" in text.lower()
        assert "PASS" in text or "pass" in text.lower()
        assert "0.05" in text or "0.0" in text


class TestFormatPipelineResult:
    """format_pipeline_result produces a multi-line report."""

    def test_format(self) -> None:
        from wowsim.pipeline import format_pipeline_result
        from wowsim.models import PipelineConfig, PipelineResult, StageResult

        config = PipelineConfig(
            fault_id="latency-spike", action="activate", version="1.0.0"
        )
        stages = [
            StageResult(
                stage="build", passed=True, message="OK",
                duration_seconds=0.1, health_status="healthy",
            ),
            StageResult(
                stage="validate", passed=True, message="OK",
                duration_seconds=0.2, health_status="healthy",
            ),
            StageResult(
                stage="canary", passed=True, message="Canary passed",
                duration_seconds=5.0, health_status="healthy",
            ),
            StageResult(
                stage="promote", passed=True, message="Promoted",
                duration_seconds=0.0,
            ),
        ]
        result = PipelineResult(
            config=config,
            stages=stages,
            outcome="promoted",
            total_duration_seconds=5.3,
        )
        text = format_pipeline_result(result)
        assert "latency-spike" in text
        assert "activate" in text
        assert "1.0.0" in text
        assert "PROMOTED" in text or "promoted" in text.lower()
        assert "build" in text.lower()
        assert "canary" in text.lower()


# ============================================================
# Group G: Orchestration with monkeypatch (3 tests)
# ============================================================


def _make_healthy_report():
    """Helper: a healthy, reachable HealthReport."""
    from datetime import datetime, timezone
    from wowsim.models import HealthReport

    return HealthReport(
        timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
        status="healthy",
        server_reachable=True,
    )


def _make_critical_report():
    """Helper: a critical HealthReport."""
    from datetime import datetime, timezone
    from wowsim.models import HealthReport

    return HealthReport(
        timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
        status="critical",
        server_reachable=True,
    )


def _make_unreachable_report():
    """Helper: an unreachable HealthReport."""
    from datetime import datetime, timezone
    from wowsim.models import HealthReport

    return HealthReport(
        timestamp=datetime(2026, 2, 24, tzinfo=timezone.utc),
        status="healthy",
        server_reachable=False,
    )


class TestOrchestratorHappyPath:
    """Pipeline promotes when all stages pass."""

    def test_promote(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from wowsim import pipeline
        from wowsim.models import PipelineConfig

        report = _make_healthy_report()
        monkeypatch.setattr(pipeline, "_get_health_report", lambda _cfg: report)
        monkeypatch.setattr(pipeline, "_execute_deploy_action", lambda _cfg: None)

        config = PipelineConfig(
            fault_id="latency-spike",
            action="activate",
            canary_duration_seconds=0.1,
            canary_poll_interval_seconds=0.05,
        )
        result = pipeline.run_pipeline(config)
        assert result.outcome == "promoted"
        stage_names = [s.stage for s in result.stages]
        assert "build" in stage_names
        assert "validate" in stage_names
        assert "canary" in stage_names
        assert "promote" in stage_names
        assert all(s.passed for s in result.stages)


class TestOrchestratorRollbackOnCritical:
    """Pipeline rolls back when canary detects critical health."""

    def test_rollback(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from wowsim import pipeline
        from wowsim.models import PipelineConfig

        healthy = _make_healthy_report()
        critical = _make_critical_report()
        call_count = {"n": 0}

        def mock_health(_cfg: PipelineConfig) -> HealthReport:
            call_count["n"] += 1
            # First two calls (build + validate) healthy, canary polls critical
            if call_count["n"] <= 2:
                return healthy
            return critical

        monkeypatch.setattr(pipeline, "_get_health_report", mock_health)
        monkeypatch.setattr(pipeline, "_execute_deploy_action", lambda _cfg: None)
        monkeypatch.setattr(pipeline, "_execute_rollback", lambda _cfg: None)

        config = PipelineConfig(
            fault_id="latency-spike",
            action="activate",
            canary_duration_seconds=0.1,
            canary_poll_interval_seconds=0.05,
            rollback_on="critical",
        )
        result = pipeline.run_pipeline(config)
        assert result.outcome == "rolled_back"
        stage_names = [s.stage for s in result.stages]
        assert "rollback" in stage_names


class TestOrchestratorAbortOnBuildFail:
    """Pipeline aborts when build preconditions fail."""

    def test_abort(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from wowsim import pipeline
        from wowsim.models import PipelineConfig

        unreachable = _make_unreachable_report()
        monkeypatch.setattr(pipeline, "_get_health_report", lambda _cfg: unreachable)

        config = PipelineConfig(
            fault_id="latency-spike",
            action="activate",
        )
        result = pipeline.run_pipeline(config)
        assert result.outcome == "aborted"
        assert len(result.stages) == 1
        assert result.stages[0].stage == "build"
        assert result.stages[0].passed is False


# ============================================================
# Group H: CLI Integration (2 tests)
# ============================================================


class TestCLIDeployHelp:
    """deploy --help shows expected options."""

    def test_help(self) -> None:
        from click.testing import CliRunner
        from wowsim.cli import main

        runner = CliRunner()
        result = runner.invoke(main, ["deploy", "--help"])
        assert result.exit_code == 0, result.output
        assert "--fault-id" in result.output
        assert "--action" in result.output
        assert "--canary-duration" in result.output
        assert "--rollback-on" in result.output


class TestCLIDeployMissingFaultId:
    """deploy without --fault-id exits with error."""

    def test_missing_fault_id(self) -> None:
        from click.testing import CliRunner
        from wowsim.cli import main

        runner = CliRunner()
        result = runner.invoke(main, ["deploy"])
        assert result.exit_code != 0
