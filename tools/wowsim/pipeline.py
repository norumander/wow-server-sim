"""Hotfix deployment pipeline for the WoW server simulator.

Orchestrates a staged deployment lifecycle (build → validate → canary →
promote/rollback) by composing existing tools (health_check, fault_trigger)
into a multi-stage pipeline with automated health gates and rollback.
"""

from __future__ import annotations

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
