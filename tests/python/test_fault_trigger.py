"""Tests for the fault injection trigger client (wowsim.fault_trigger).

Covers: Pydantic models, duration parsing, ControlClient commands,
error handling, and CLI integration.
"""

from __future__ import annotations

import pytest

from wowsim.models import (
    ControlResponse,
    FaultActivateRequest,
    FaultInfo,
)


# ---------------------------------------------------------------------------
# Group A: Control channel Pydantic models
# ---------------------------------------------------------------------------


class TestFaultActivateRequestSerializes:
    """FaultActivateRequest.model_dump() has correct command/fault_id/params."""

    def test_default_fields(self) -> None:
        req = FaultActivateRequest(fault_id="latency-spike")
        data = req.model_dump()
        assert data["command"] == "activate"
        assert data["fault_id"] == "latency-spike"
        assert data["params"] == {}
        assert data["target_zone_id"] == 0
        assert data["duration_ticks"] == 0

    def test_with_params(self) -> None:
        req = FaultActivateRequest(
            fault_id="latency-spike",
            params={"delay_ms": 300},
            target_zone_id=2,
            duration_ticks=100,
        )
        data = req.model_dump()
        assert data["params"] == {"delay_ms": 300}
        assert data["target_zone_id"] == 2
        assert data["duration_ticks"] == 100


class TestControlResponseParsesSuccess:
    """ControlResponse.model_validate_json() with success responses."""

    def test_activate_response(self) -> None:
        raw = '{"success":true,"command":"activate","fault_id":"latency-spike"}'
        resp = ControlResponse.model_validate_json(raw)
        assert resp.success is True
        assert resp.command == "activate"
        assert resp.fault_id == "latency-spike"
        assert resp.error is None

    def test_list_response(self) -> None:
        raw = (
            '{"success":true,"command":"list","faults":'
            '[{"id":"latency-spike","mode":"tick_scoped","active":false}]}'
        )
        resp = ControlResponse.model_validate_json(raw)
        assert resp.success is True
        assert resp.faults is not None
        assert len(resp.faults) == 1
        assert resp.faults[0].id == "latency-spike"
        assert resp.faults[0].active is False


class TestControlResponseParsesError:
    """ControlResponse.model_validate_json() with error responses."""

    def test_error_fields(self) -> None:
        raw = '{"success":false,"error":"Unknown fault: bad-id"}'
        resp = ControlResponse.model_validate_json(raw)
        assert resp.success is False
        assert resp.error == "Unknown fault: bad-id"
        assert resp.command is None
        assert resp.fault_id is None


# ---------------------------------------------------------------------------
# Group B: Duration parsing
# ---------------------------------------------------------------------------


from wowsim.fault_trigger import TICKS_PER_SECOND, parse_duration


class TestParseDurationSeconds:
    """parse_duration('5s') → 100 ticks at 20 Hz."""

    def test_whole_seconds(self) -> None:
        assert parse_duration("5s") == 100
        assert parse_duration("10s") == 200

    def test_fractional_seconds(self) -> None:
        assert parse_duration("0.5s") == 10
        assert parse_duration("1.5s") == 30

    def test_ticks_per_second_constant(self) -> None:
        assert TICKS_PER_SECOND == 20


class TestParseDurationTicksAndInvalid:
    """parse_duration('100t') → 100; invalid strings raise ValueError."""

    def test_tick_suffix(self) -> None:
        assert parse_duration("100t") == 100
        assert parse_duration("30t") == 30

    def test_invalid_raises(self) -> None:
        with pytest.raises(ValueError):
            parse_duration("5")
        with pytest.raises(ValueError):
            parse_duration("abc")
        with pytest.raises(ValueError):
            parse_duration("")
