"""Hotfix deployment pipeline for the WoW server simulator.

Orchestrates a staged deployment lifecycle (build → validate → canary →
promote/rollback) by composing existing tools (health_check, fault_trigger)
into a multi-stage pipeline with automated health gates and rollback.
"""

from __future__ import annotations

import time
from pathlib import Path

from wowsim.models import (
    HealthReport,
    PipelineConfig,
    PipelineResult,
    StageResult,
)


# ---------------------------------------------------------------------------
# Pure gate functions (no I/O, fully testable)
# ---------------------------------------------------------------------------


def check_build_preconditions(reachable: bool, status: str) -> StageResult:
    """Build stage gate: server must be reachable and not critical.

    Degraded is allowed — the deploy might be the fix.
    """
    if not reachable:
        return StageResult(
            stage="build",
            passed=False,
            message="Server unreachable — cannot deploy",
            health_status=status,
        )
    if status == "critical":
        return StageResult(
            stage="build",
            passed=False,
            message="Server is critical — resolve before deploying",
            health_status=status,
        )
    return StageResult(
        stage="build",
        passed=True,
        message="Build preconditions met",
        health_status=status,
    )


def check_validate_gate(report: HealthReport) -> StageResult:
    """Validate stage gate: server must be reachable."""
    if not report.server_reachable:
        return StageResult(
            stage="validate",
            passed=False,
            message="Server unreachable during validation",
            health_status=report.status,
        )
    return StageResult(
        stage="validate",
        passed=True,
        message="Validation passed",
        health_status=report.status,
    )


def evaluate_canary_health(
    samples: list[str], threshold: str
) -> tuple[bool, str]:
    """Evaluate canary health samples against a rollback threshold.

    Returns (passed, message). Fails on first sample at or above threshold.
    """
    severity_order = {"healthy": 0, "degraded": 1, "critical": 2}
    threshold_level = severity_order.get(threshold, 2)

    for i, sample in enumerate(samples):
        sample_level = severity_order.get(sample, 2)
        if sample_level >= threshold_level:
            return (
                False,
                f"Canary failed: sample {i + 1}/{len(samples)} "
                f"was {sample} (threshold: {threshold})",
            )
    return (True, f"Canary passed: {len(samples)} samples all below {threshold}")


def determine_rollback_action(config: PipelineConfig) -> tuple[str, str]:
    """Determine the reverse action for rollback.

    activate → deactivate, deactivate → activate.
    """
    reverse = "deactivate" if config.action == "activate" else "activate"
    return (reverse, config.fault_id)


# ---------------------------------------------------------------------------
# Formatting (no I/O)
# ---------------------------------------------------------------------------


def format_stage_result(result: StageResult) -> str:
    """One-line stage summary: [PASS/FAIL] stage (duration) — message."""
    tag = "PASS" if result.passed else "FAIL"
    return f"[{tag}] {result.stage:<10s} ({result.duration_seconds:.2f}s) — {result.message}"


def format_pipeline_result(result: PipelineResult) -> str:
    """Multi-line pipeline report with header, stages, and outcome."""
    lines: list[str] = []
    lines.append("=== Hotfix Pipeline Report ===")
    lines.append(f"Version: {result.config.version}")
    lines.append(f"Fault:   {result.config.fault_id}")
    lines.append(f"Action:  {result.config.action}")
    lines.append("")
    lines.append("Stages:")
    for stage in result.stages:
        lines.append(f"  {format_stage_result(stage)}")
    lines.append("")
    lines.append(f"Outcome: {result.outcome.upper()}")
    lines.append(f"Total:   {result.total_duration_seconds:.2f}s")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# I/O wrappers (thin, mockable)
# ---------------------------------------------------------------------------


def _get_health_report(config: PipelineConfig) -> HealthReport:
    """Fetch a health report using the config's connection settings."""
    from wowsim.health_check import build_health_report

    log_path = Path(config.log_file) if config.log_file else None
    return build_health_report(
        log_path=log_path,
        game_host=config.game_host,
        game_port=config.game_port,
        control_host=config.control_host,
        control_port=config.control_port,
        skip_faults=True,
    )


def _execute_deploy_action(config: PipelineConfig) -> None:
    """Execute the deploy action (activate or deactivate a fault)."""
    from wowsim.fault_trigger import activate_fault, deactivate_fault

    if config.action == "activate":
        activate_fault(
            config.control_host,
            config.control_port,
            config.fault_id,
            params=config.params,
            target_zone_id=config.target_zone_id,
            duration_ticks=config.duration_ticks,
        )
    else:
        deactivate_fault(config.control_host, config.control_port, config.fault_id)


def _execute_rollback(config: PipelineConfig) -> None:
    """Reverse the deploy action for rollback."""
    from wowsim.fault_trigger import activate_fault, deactivate_fault

    if config.action == "activate":
        deactivate_fault(config.control_host, config.control_port, config.fault_id)
    else:
        activate_fault(
            config.control_host,
            config.control_port,
            config.fault_id,
            params=config.params,
            target_zone_id=config.target_zone_id,
            duration_ticks=config.duration_ticks,
        )


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------


def run_pipeline(config: PipelineConfig) -> PipelineResult:
    """Run the full hotfix deployment pipeline.

    Stages: build → validate → canary → promote/rollback.
    Returns a PipelineResult with all stages and final outcome.
    """
    pipeline_start = time.monotonic()
    stages: list[StageResult] = []

    # --- BUILD ---
    t0 = time.monotonic()
    report = _get_health_report(config)
    build_result = check_build_preconditions(report.server_reachable, report.status)
    build_result.duration_seconds = time.monotonic() - t0
    stages.append(build_result)

    if not build_result.passed:
        return PipelineResult(
            config=config,
            stages=stages,
            outcome="aborted",
            total_duration_seconds=time.monotonic() - pipeline_start,
        )

    # --- VALIDATE ---
    t0 = time.monotonic()
    report = _get_health_report(config)
    validate_result = check_validate_gate(report)
    validate_result.duration_seconds = time.monotonic() - t0
    stages.append(validate_result)

    if not validate_result.passed:
        return PipelineResult(
            config=config,
            stages=stages,
            outcome="aborted",
            total_duration_seconds=time.monotonic() - pipeline_start,
        )

    # --- CANARY ---
    t0 = time.monotonic()
    _execute_deploy_action(config)

    samples: list[str] = []
    deadline = time.monotonic() + config.canary_duration_seconds
    while time.monotonic() < deadline:
        time.sleep(config.canary_poll_interval_seconds)
        canary_report = _get_health_report(config)
        samples.append(canary_report.status)
        passed, message = evaluate_canary_health(samples, config.rollback_on)
        if not passed:
            break

    canary_passed, canary_message = evaluate_canary_health(samples, config.rollback_on)
    canary_result = StageResult(
        stage="canary",
        passed=canary_passed,
        message=canary_message,
        duration_seconds=time.monotonic() - t0,
        health_status=samples[-1] if samples else None,
        details={"samples": samples},
    )
    stages.append(canary_result)

    if not canary_passed:
        # --- ROLLBACK ---
        t0 = time.monotonic()
        _execute_rollback(config)
        rollback_result = StageResult(
            stage="rollback",
            passed=True,
            message="Rollback executed — deploy action reversed",
            duration_seconds=time.monotonic() - t0,
        )
        stages.append(rollback_result)
        return PipelineResult(
            config=config,
            stages=stages,
            outcome="rolled_back",
            total_duration_seconds=time.monotonic() - pipeline_start,
        )

    # --- PROMOTE ---
    promote_result = StageResult(
        stage="promote",
        passed=True,
        message="Canary passed — deploy promoted",
    )
    stages.append(promote_result)
    return PipelineResult(
        config=config,
        stages=stages,
        outcome="promoted",
        total_duration_seconds=time.monotonic() - pipeline_start,
    )
