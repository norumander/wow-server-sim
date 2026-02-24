"""Fault injection trigger client for the control channel.

Provides an async TCP client (ControlClient), sync convenience wrappers
for CLI use, and duration parsing utilities.
"""

from __future__ import annotations

import asyncio
import json
from typing import Any

from wowsim.models import (
    ControlResponse,
    FaultActivateRequest,
    FaultDeactivateAllRequest,
    FaultDeactivateRequest,
    FaultInfo,
    FaultListRequest,
    FaultStatusRequest,
)

TICKS_PER_SECOND: int = 20
"""WoW server tick rate (matching C++ game loop at 20 Hz)."""


class ControlClientError(Exception):
    """Raised on error responses or connection failures."""


def parse_duration(duration_str: str) -> int:
    """Parse a human-friendly duration string into server ticks.

    Accepted formats:
        '5s'   → 100 ticks  (seconds × TICKS_PER_SECOND)
        '0.5s' → 10 ticks
        '100t' → 100 ticks  (raw tick count)

    Raises:
        ValueError: If the string is empty or has no recognized suffix.
    """
    if not duration_str:
        raise ValueError("Empty duration string")

    if duration_str.endswith("s"):
        try:
            seconds = float(duration_str[:-1])
        except ValueError:
            raise ValueError(
                f"Invalid duration: {duration_str!r} — expected number before 's'"
            )
        return int(seconds * TICKS_PER_SECOND)

    if duration_str.endswith("t"):
        try:
            ticks = int(duration_str[:-1])
        except ValueError:
            raise ValueError(
                f"Invalid duration: {duration_str!r} — expected integer before 't'"
            )
        return ticks

    raise ValueError(
        f"Invalid duration: {duration_str!r}"
        " — must end with 's' (seconds) or 't' (ticks)"
    )


# ---------------------------------------------------------------------------
# Async TCP client for the control channel
# ---------------------------------------------------------------------------


class ControlClient:
    """Async TCP client for the C++ server's control channel.

    Usage::

        async with ControlClient("localhost", 8081) as client:
            resp = await client.activate("latency-spike", params={"delay_ms": 300})
            print(resp.success)
    """

    def __init__(self, host: str, port: int) -> None:
        self._host = host
        self._port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None

    async def connect(self) -> None:
        """Open TCP connection to the control channel."""
        self._reader, self._writer = await asyncio.open_connection(
            self._host, self._port
        )

    async def close(self) -> None:
        """Close the TCP connection."""
        if self._writer is not None:
            self._writer.close()
            await self._writer.wait_closed()
            self._writer = None
            self._reader = None

    async def __aenter__(self) -> ControlClient:
        await self.connect()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.close()

    async def _send_command(self, request: dict[str, Any]) -> ControlResponse:
        """Send a JSON command and read the newline-delimited response."""
        if self._writer is None or self._reader is None:
            raise ControlClientError("Not connected — call connect() first")

        payload = json.dumps(request) + "\n"
        self._writer.write(payload.encode())
        await self._writer.drain()

        line = await self._reader.readline()
        if not line:
            raise ControlClientError("Connection closed by server")

        resp = ControlResponse.model_validate_json(line)
        if not resp.success:
            raise ControlClientError(resp.error or "Unknown server error")
        return resp

    async def activate(
        self,
        fault_id: str,
        *,
        params: dict[str, Any] | None = None,
        target_zone_id: int = 0,
        duration_ticks: int = 0,
    ) -> ControlResponse:
        """Activate a fault on the server."""
        req = FaultActivateRequest(
            fault_id=fault_id,
            params=params or {},
            target_zone_id=target_zone_id,
            duration_ticks=duration_ticks,
        )
        return await self._send_command(req.model_dump())

    async def deactivate(self, fault_id: str) -> ControlResponse:
        """Deactivate a specific fault."""
        req = FaultDeactivateRequest(fault_id=fault_id)
        return await self._send_command(req.model_dump())

    async def deactivate_all(self) -> ControlResponse:
        """Deactivate all active faults."""
        req = FaultDeactivateAllRequest()
        return await self._send_command(req.model_dump())

    async def status(self, fault_id: str) -> ControlResponse:
        """Query the status of a specific fault."""
        req = FaultStatusRequest(fault_id=fault_id)
        return await self._send_command(req.model_dump())

    async def list_faults(self) -> ControlResponse:
        """List all registered faults."""
        req = FaultListRequest()
        return await self._send_command(req.model_dump())


# ---------------------------------------------------------------------------
# Sync wrappers (for CLI use)
# ---------------------------------------------------------------------------


def _run_command(host: str, port: int, coro_factory: Any) -> ControlResponse:
    """Run an async command in a fresh event loop."""

    async def _run() -> ControlResponse:
        async with ControlClient(host, port) as client:
            return await coro_factory(client)

    return asyncio.run(_run())


def activate_fault(
    host: str,
    port: int,
    fault_id: str,
    *,
    params: dict[str, Any] | None = None,
    target_zone_id: int = 0,
    duration_ticks: int = 0,
) -> ControlResponse:
    """Activate a fault (sync wrapper)."""
    return _run_command(
        host,
        port,
        lambda c: c.activate(
            fault_id,
            params=params,
            target_zone_id=target_zone_id,
            duration_ticks=duration_ticks,
        ),
    )


def deactivate_fault(host: str, port: int, fault_id: str) -> ControlResponse:
    """Deactivate a specific fault (sync wrapper)."""
    return _run_command(host, port, lambda c: c.deactivate(fault_id))


def deactivate_all_faults(host: str, port: int) -> ControlResponse:
    """Deactivate all active faults (sync wrapper)."""
    return _run_command(host, port, lambda c: c.deactivate_all())


def fault_status(host: str, port: int, fault_id: str) -> ControlResponse:
    """Query the status of a fault (sync wrapper)."""
    return _run_command(host, port, lambda c: c.status(fault_id))


def list_all_faults(host: str, port: int) -> ControlResponse:
    """List all registered faults (sync wrapper)."""
    return _run_command(host, port, lambda c: c.list_faults())


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------


def format_fault_info(info: FaultInfo) -> str:
    """One-line summary of a fault's status.

    Example: 'latency-spike  tick_scoped  ACTIVE  (delay_ms=200)'
    """
    status_str = "ACTIVE" if info.active else "inactive"
    config_str = ""
    if info.config:
        pairs = ", ".join(f"{k}={v}" for k, v in info.config.items())
        config_str = f"  ({pairs})"
    return f"{info.id:<22s} {info.mode:<14s} {status_str}{config_str}"


def format_fault_list(faults: list[FaultInfo]) -> str:
    """Table of all faults with status."""
    header = f"{'FAULT ID':<22s} {'MODE':<14s} STATUS"
    separator = "-" * len(header)
    lines = [header, separator]
    for f in faults:
        lines.append(format_fault_info(f))
    return "\n".join(lines)
