"""Fault injection trigger client for the control channel.

Provides an async TCP client (ControlClient), sync convenience wrappers
for CLI use, and duration parsing utilities.
"""

from __future__ import annotations

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
        raise ValueError(f"Empty duration string")

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
        f"Invalid duration: {duration_str!r} — must end with 's' (seconds) or 't' (ticks)"
    )
