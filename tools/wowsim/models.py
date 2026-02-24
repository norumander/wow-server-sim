"""Pydantic models for server telemetry entries and analysis results."""

from __future__ import annotations

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel


class TelemetryEntry(BaseModel):
    """A single telemetry log entry from the C++ server."""

    v: int
    timestamp: datetime
    type: Literal["metric", "event", "health", "error"]
    component: str
    message: str
    data: dict[str, Any] = {}


class LogSummary(BaseModel):
    """Aggregate statistics from a telemetry log."""

    total_entries: int
    entries_by_type: dict[str, int]
    entries_by_component: dict[str, int]
    error_count: int
    time_range_start: datetime | None
    time_range_end: datetime | None
    duration_seconds: float


class Anomaly(BaseModel):
    """A detected anomaly in the telemetry stream."""

    type: str
    severity: Literal["warning", "critical"]
    timestamp: datetime
    message: str
    details: dict[str, Any] = {}


class ParseResult(BaseModel):
    """Complete result from log parsing."""

    entries: list[TelemetryEntry]
    summary: LogSummary
    anomalies: list[Anomaly]
