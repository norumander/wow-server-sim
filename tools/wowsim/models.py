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
