"""Server lifecycle management for the interactive demo.

Pure functions for binary detection and telemetry cleanup,
plus a ServerProcess class for starting/stopping the C++ server.
"""

from __future__ import annotations

import subprocess
import time
from pathlib import Path


# ---------------------------------------------------------------------------
# Candidate binary paths (same priority order as demo.sh)
# ---------------------------------------------------------------------------

_CANDIDATE_PATHS: list[str] = [
    "build/Debug/wow-server-sim.exe",
    "build/wow-server-sim.exe",
    "build/wow-server-sim",
    "wow-server-sim",
]


# ---------------------------------------------------------------------------
# Pure functions (no subprocess, fully testable)
# ---------------------------------------------------------------------------


def find_server_binary(project_root: Path) -> Path | None:
    """Check candidate paths in priority order and return the first that exists.

    Returns None if no binary is found.
    """
    for candidate in _CANDIDATE_PATHS:
        path = project_root / candidate
        if path.is_file():
            return path
    return None


def clean_telemetry(path: Path) -> None:
    """Remove a stale telemetry file if it exists."""
    if path.exists():
        path.unlink()


def default_telemetry_path(project_root: Path) -> Path:
    """Return the default telemetry file path for a project root."""
    return project_root / "telemetry.jsonl"


# ---------------------------------------------------------------------------
# Server process lifecycle
# ---------------------------------------------------------------------------


class ServerProcess:
    """Manages the C++ server binary as a subprocess.

    Usage::

        sp = ServerProcess(binary, telemetry_path)
        sp.start(timeout=10)
        # ... run dashboard ...
        sp.stop()
    """

    def __init__(
        self,
        binary: Path,
        telemetry_path: Path,
        port: int = 8080,
        control_port: int = 8081,
    ) -> None:
        self._binary = binary
        self._telemetry_path = telemetry_path
        self._port = port
        self._control_port = control_port
        self._process: subprocess.Popen | None = None

    @property
    def running(self) -> bool:
        """Whether the server process is alive."""
        return self._process is not None and self._process.poll() is None

    def start(self, timeout: float = 10.0) -> None:
        """Start the server and wait for telemetry output as readiness signal.

        Raises RuntimeError if the server does not produce telemetry
        within the timeout.
        """
        # Touch empty telemetry file so the dashboard file reader works
        self._telemetry_path.touch()

        self._process = subprocess.Popen(
            [
                str(self._binary),
                "--port", str(self._port),
                "--control-port", str(self._control_port),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        # Wait for telemetry file to grow (server writes on startup)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._telemetry_path.exists() and self._telemetry_path.stat().st_size > 0:
                return
            if self._process.poll() is not None:
                raise RuntimeError(
                    f"Server exited with code {self._process.returncode}"
                )
            time.sleep(0.2)

        raise RuntimeError(
            f"Server did not produce telemetry within {timeout}s"
        )

    def stop(self) -> None:
        """Terminate the server process if running."""
        if self._process is None:
            return
        if self._process.poll() is None:
            self._process.terminate()
            try:
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
        self._process = None
