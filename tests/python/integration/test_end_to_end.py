"""End-to-end integration tests composing all Python tools.

Verifies the full workflow: connect -> play -> inject fault -> detect -> recover.
Each test exercises multiple tools together in realistic operator scenarios.

Tests:
  13 — Full pipeline: connect, inject, detect, recover
  14 — Fifty-player stress with health report
  15 — All fault scenarios produce detectable telemetry
  16 — Health report format includes all sections
"""

from __future__ import annotations

import json
from pathlib import Path

from wowsim.fault_trigger import activate_fault, deactivate_fault
from wowsim.health_check import build_health_report, format_health_report
from wowsim.log_parser import detect_anomalies, parse_file
from wowsim.mock_client import run_spawn
from wowsim.models import ClientConfig


class TestFullPipelineConnectInjectDetectRecover:
    """All 5 tools compose: spawn -> activate -> detect -> deactivate -> recover."""

    def test_full_pipeline(
        self,
        mock_game_server: dict,
        mock_control_server: dict,
        recovery_scenario_log: Path,
    ) -> None:
        game_host, game_port = mock_game_server["host"], mock_game_server["port"]
        ctrl_host, ctrl_port = mock_control_server["host"], mock_control_server["port"]

        # Step 1: Spawn clients
        cfg = ClientConfig(
            host=game_host,
            port=game_port,
            actions_per_second=10.0,
            duration_seconds=0.5,
        )
        spawn_result = run_spawn(cfg, 5)
        assert spawn_result.successful_connections == 5

        # Step 2: Activate fault
        activate_resp = activate_fault(
            ctrl_host, ctrl_port, "latency-spike", params={"delay_ms": 200}
        )
        assert activate_resp.success is True

        # Step 3: Detect anomaly from telemetry
        entries = parse_file(recovery_scenario_log)
        anomalies = detect_anomalies(entries)
        assert len(anomalies) >= 1

        # Step 4: Deactivate fault
        deactivate_resp = deactivate_fault(ctrl_host, ctrl_port, "latency-spike")
        assert deactivate_resp.success is True

        # Step 5: Verify recovery — recovery phase (ticks 16-25) has no latency issues
        recovery_entries = [
            e
            for e in entries
            if e.component == "game_loop" and e.data.get("tick", 0) >= 16
        ]
        recovery_anomalies = detect_anomalies(recovery_entries)
        latency_anomalies = [
            a for a in recovery_anomalies if a.type == "latency_spike"
        ]
        assert len(latency_anomalies) == 0


class TestFiftyPlayerStressWithHealthReport:
    """50 clients + health report -> HEALTHY, 50 players, 0 overruns."""

    def test_fifty_player_health(
        self, mock_game_server: dict, fifty_player_log: Path
    ) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]

        # Spawn 50 clients
        cfg = ClientConfig(
            host=host, port=port, actions_per_second=10.0, duration_seconds=0.5
        )
        spawn_result = run_spawn(cfg, 50)
        assert spawn_result.successful_connections == 50

        # Build health report from telemetry
        report = build_health_report(
            log_path=fifty_player_log,
            game_host=host,
            game_port=port,
            skip_faults=True,
        )
        assert report.status == "healthy"
        assert report.connected_players == 50
        assert report.tick is not None
        assert report.tick.overrun_count == 0


class TestAllFaultScenariosProduceDetectableTelemetry:
    """F1->latency_spike, F2->unexpected_disconnect, F3->error_burst, F4->activate."""

    def test_all_fault_scenarios(
        self,
        mock_control_server: dict,
        latency_spike_log: Path,
        session_crash_log: Path,
        zone_crash_log: Path,
        tmp_path: Path,
    ) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]

        # F1: Latency spike -> latency_spike anomaly
        entries_f1 = parse_file(latency_spike_log)
        anomalies_f1 = detect_anomalies(entries_f1)
        assert any(a.type == "latency_spike" for a in anomalies_f1)

        # F2: Session crash -> unexpected_disconnect anomaly
        entries_f2 = parse_file(session_crash_log)
        anomalies_f2 = detect_anomalies(entries_f2)
        assert any(a.type == "unexpected_disconnect" for a in anomalies_f2)

        # F3: Error burst -> error_burst anomaly (synthesized log)
        lines = []
        for i in range(6):
            lines.append(
                json.dumps({
                    "v": 1,
                    "timestamp": f"2026-02-24T12:00:00.{i:03d}Z",
                    "type": "error",
                    "component": "combat",
                    "message": f"Processing error {i}",
                    "data": {"detail": f"err_{i}"},
                })
            )
        error_burst_log = tmp_path / "error_burst.jsonl"
        error_burst_log.write_text("\n".join(lines) + "\n")
        entries_f3 = parse_file(error_burst_log)
        anomalies_f3 = detect_anomalies(entries_f3)
        assert any(a.type == "error_burst" for a in anomalies_f3)

        # F4: Memory pressure -> activate succeeds via control channel
        resp = activate_fault(host, port, "memory-pressure", params={"megabytes": 64})
        assert resp.success is True


class TestHealthReportFormatIncludesAllSections:
    """format_health_report output contains all expected sections."""

    def test_format_sections(
        self, mock_game_server: dict, recovery_scenario_log: Path
    ) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        report = build_health_report(
            log_path=recovery_scenario_log,
            game_host=host,
            game_port=port,
            skip_faults=True,
        )
        text = format_health_report(report)
        assert "WoW Server Health Report" in text
        assert "Status:" in text
        assert "Server:" in text
        assert "Tick Rate:" in text
        assert "Connected Players:" in text
        assert "Anomalies:" in text
