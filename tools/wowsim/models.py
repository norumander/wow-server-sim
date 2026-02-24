"""Pydantic models for telemetry, analysis, and control channel protocol."""

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


# ---------------------------------------------------------------------------
# Control Channel Protocol Models
# ---------------------------------------------------------------------------


class FaultActivateRequest(BaseModel):
    """Request to activate a fault via the control channel."""

    command: Literal["activate"] = "activate"
    fault_id: str
    params: dict[str, Any] = {}
    target_zone_id: int = 0
    duration_ticks: int = 0


class FaultDeactivateRequest(BaseModel):
    """Request to deactivate a specific fault."""

    command: Literal["deactivate"] = "deactivate"
    fault_id: str


class FaultDeactivateAllRequest(BaseModel):
    """Request to deactivate all active faults."""

    command: Literal["deactivate_all"] = "deactivate_all"


class FaultStatusRequest(BaseModel):
    """Request for the status of a specific fault."""

    command: Literal["status"] = "status"
    fault_id: str


class FaultListRequest(BaseModel):
    """Request to list all registered faults."""

    command: Literal["list"] = "list"


class FaultInfo(BaseModel):
    """Status of a single fault, as returned by the server."""

    id: str
    mode: str
    active: bool
    activations: int = 0
    ticks_elapsed: int = 0
    config: dict[str, Any] = {}


class ControlResponse(BaseModel):
    """Generic control channel response (covers all command types)."""

    success: bool
    command: str | None = None
    fault_id: str | None = None
    error: str | None = None
    status: FaultInfo | None = None
    faults: list[FaultInfo] | None = None


# ---------------------------------------------------------------------------
# Health Check Models
# ---------------------------------------------------------------------------


class TickHealth(BaseModel):
    """Tick rate stability metrics from recent telemetry."""

    total_ticks: int
    avg_duration_ms: float
    max_duration_ms: float
    min_duration_ms: float
    overrun_count: int
    overrun_pct: float


class ZoneHealthSummary(BaseModel):
    """Per-zone health from recent telemetry."""

    zone_id: int
    state: str
    tick_count: int
    error_count: int
    avg_tick_duration_ms: float


class HealthReport(BaseModel):
    """Complete server health report."""

    timestamp: datetime
    status: Literal["healthy", "degraded", "critical"]
    server_reachable: bool
    tick: TickHealth | None = None
    zones: list[ZoneHealthSummary] = []
    connected_players: int = 0
    anomalies: list[Anomaly] = []
    active_faults: list[FaultInfo] = []
    error_count: int = 0
    uptime_ticks: int = 0
