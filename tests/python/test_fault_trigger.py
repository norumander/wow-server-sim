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


from wowsim.fault_trigger import (
    TICKS_PER_SECOND,
    ControlClient,
    ControlClientError,
    activate_fault,
    deactivate_all_faults,
    deactivate_fault,
    fault_status,
    list_all_faults,
    parse_duration,
)


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


# ---------------------------------------------------------------------------
# Group C: Client commands via sync wrappers
# ---------------------------------------------------------------------------


class TestActivateSendsCorrectJson:
    """activate_fault() sends correct JSON and returns ControlResponse."""

    def test_activate_request_format(self, mock_control_server: dict) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]
        resp = activate_fault(
            host,
            port,
            "latency-spike",
            params={"delay_ms": 300},
            duration_ticks=100,
            target_zone_id=1,
        )
        assert resp.success is True
        assert resp.fault_id == "latency-spike"

        received = mock_control_server["received"]
        assert len(received) == 1
        req = received[0]
        assert req["command"] == "activate"
        assert req["fault_id"] == "latency-spike"
        assert req["params"] == {"delay_ms": 300}
        assert req["duration_ticks"] == 100
        assert req["target_zone_id"] == 1


class TestDeactivateSendsCorrectJson:
    """deactivate_fault() sends correct JSON wire format."""

    def test_deactivate_request_format(self, mock_control_server: dict) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]
        resp = deactivate_fault(host, port, "latency-spike")
        assert resp.success is True

        received = mock_control_server["received"]
        assert len(received) == 1
        req = received[0]
        assert req["command"] == "deactivate"
        assert req["fault_id"] == "latency-spike"


class TestStatusReturnsFaultInfo:
    """fault_status() returns response with .status containing FaultInfo."""

    def test_status_has_fault_info(self, mock_control_server: dict) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]
        resp = fault_status(host, port, "latency-spike")
        assert resp.success is True
        assert resp.status is not None
        assert resp.status.id == "latency-spike"
        assert resp.status.mode == "tick_scoped"
        assert resp.status.active is True
        assert resp.status.activations == 1


class TestListReturnsFaultList:
    """list_all_faults() returns response with .faults list."""

    def test_list_has_all_faults(self, mock_control_server: dict) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]
        resp = list_all_faults(host, port)
        assert resp.success is True
        assert resp.faults is not None
        assert len(resp.faults) == 4
        fault_ids = [f.id for f in resp.faults]
        assert "latency-spike" in fault_ids
        assert "memory-pressure" in fault_ids


# ---------------------------------------------------------------------------
# Group D: Error handling
# ---------------------------------------------------------------------------


class TestServerErrorRaisesClientError:
    """Mock returning success=false raises ControlClientError."""

    def test_error_response(self, mock_control_server: dict) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]
        mock_control_server["responses"]["activate"] = {
            "success": False,
            "error": "Unknown fault: bad-id",
        }
        with pytest.raises(ControlClientError, match="Unknown fault"):
            activate_fault(host, port, "bad-id")


class TestConnectionRefusedRaises:
    """Connecting to a closed port raises OSError."""

    def test_connection_refused(self) -> None:
        with pytest.raises(OSError):
            activate_fault("127.0.0.1", 1, "latency-spike")


class TestNotConnectedRaises:
    """Calling a command before connect() raises ControlClientError."""

    def test_not_connected(self) -> None:
        import asyncio

        async def _test() -> None:
            client = ControlClient("127.0.0.1", 9999)
            # Do not connect — should raise on command.
            with pytest.raises(ControlClientError, match="Not connected"):
                await client.activate("latency-spike")

        asyncio.run(_test())


# ---------------------------------------------------------------------------
# Group E: CLI integration
# ---------------------------------------------------------------------------


from click.testing import CliRunner

from wowsim.cli import main as cli_main


class TestCLIActivateSuccess:
    """CliRunner invokes 'inject-fault activate', checks exit_code=0."""

    def test_activate_output(self, mock_control_server: dict) -> None:
        port = mock_control_server["port"]
        runner = CliRunner()
        result = runner.invoke(
            cli_main,
            [
                "inject-fault",
                "--port",
                str(port),
                "activate",
                "latency-spike",
                "--delay-ms",
                "200",
            ],
        )
        assert result.exit_code == 0, result.output
        assert "latency-spike" in result.output
        assert "activated" in result.output.lower() or "success" in result.output.lower()


class TestCLIListShowsFaults:
    """CliRunner invokes 'inject-fault list', output contains fault IDs."""

    def test_list_output(self, mock_control_server: dict) -> None:
        port = mock_control_server["port"]
        runner = CliRunner()
        result = runner.invoke(
            cli_main, ["inject-fault", "--port", str(port), "list"]
        )
        assert result.exit_code == 0, result.output
        assert "latency-spike" in result.output
        assert "memory-pressure" in result.output


class TestCLIConnectionRefusedError:
    """CliRunner with bad port shows error message."""

    def test_connection_error(self) -> None:
        runner = CliRunner()
        result = runner.invoke(
            cli_main,
            ["inject-fault", "--port", "1", "list"],
        )
        assert result.exit_code != 0
        assert "error" in result.output.lower() or result.exception is not None
