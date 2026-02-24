"""Integration tests for fault injection and recovery.

Verifies that fault trigger, log parser anomaly detection, and health
check tools compose correctly through the fault lifecycle:
activate -> detect -> deactivate -> verify recovery.

Tests:
  7  — Activate fault and detect latency anomaly
  8  — Deactivate fault and verify recovery
  9  — Session crash produces disconnect anomalies
  10 — Health status transitions through fault lifecycle
  11 — Zone crash detected in health report
  12 — Fault list and status query composition
"""

from __future__ import annotations

from pathlib import Path

from wowsim.fault_trigger import (
    activate_fault,
    deactivate_all_faults,
    deactivate_fault,
    fault_status,
    list_all_faults,
)
from wowsim.health_check import (
    build_health_report,
    compute_tick_health,
    compute_zone_health,
    determine_status,
)
from wowsim.log_parser import detect_anomalies, parse_file


class TestActivateFaultAndDetectLatencyAnomaly:
    """activate_fault + detect_anomalies -> latency_spike found."""

    def test_activate_and_detect(
        self, mock_control_server: dict, latency_spike_log: Path
    ) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]

        # Activate fault via control channel
        resp = activate_fault(host, port, "latency-spike", params={"delay_ms": 200})
        assert resp.success is True

        # Parse telemetry and detect anomalies
        entries = parse_file(latency_spike_log)
        anomalies = detect_anomalies(entries)
        latency_anomalies = [a for a in anomalies if a.type == "latency_spike"]
        assert len(latency_anomalies) >= 1


class TestDeactivateFaultAndVerifyRecovery:
    """deactivate succeeds, recovery-phase entries have no latency anomalies."""

    def test_deactivate_and_recovery(
        self, mock_control_server: dict, recovery_scenario_log: Path
    ) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]

        # Deactivate fault
        resp = deactivate_fault(host, port, "latency-spike")
        assert resp.success is True

        # Parse recovery-phase entries (ticks 16-25) — no latency anomalies
        entries = parse_file(recovery_scenario_log)
        recovery_entries = [
            e
            for e in entries
            if e.component == "game_loop" and e.data.get("tick", 0) >= 16
        ]
        anomalies = detect_anomalies(recovery_entries)
        latency_anomalies = [a for a in anomalies if a.type == "latency_spike"]
        assert len(latency_anomalies) == 0


class TestSessionCrashProducesDisconnectAnomalies:
    """detect_anomalies -> 3+ unexpected_disconnect."""

    def test_disconnect_anomalies(
        self, mock_control_server: dict, session_crash_log: Path
    ) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]

        # Activate session-crash fault
        resp = activate_fault(host, port, "session-crash")
        assert resp.success is True

        # Parse telemetry and detect anomalies
        entries = parse_file(session_crash_log)
        anomalies = detect_anomalies(entries)
        disconnect_anomalies = [
            a for a in anomalies if a.type == "unexpected_disconnect"
        ]
        assert len(disconnect_anomalies) >= 3


class TestHealthStatusTransitionsThroughFaultLifecycle:
    """healthy -> critical -> healthy across 3 phases of recovery_scenario_log."""

    def test_status_transitions(self, recovery_scenario_log: Path) -> None:
        entries = parse_file(recovery_scenario_log)

        # Phase 1: Healthy (ticks 1-10)
        phase1 = [
            e
            for e in entries
            if e.component == "game_loop" and 1 <= e.data.get("tick", 0) <= 10
        ]
        tick1 = compute_tick_health(phase1)
        anomalies1 = detect_anomalies(phase1)
        status1 = determine_status(tick1, [], anomalies1)
        assert status1 == "healthy"

        # Phase 2: Fault (ticks 11-15 + zone error)
        phase2 = [
            e
            for e in entries
            if (e.component == "game_loop" and 11 <= e.data.get("tick", 0) <= 15)
            or (e.type == "error")
        ]
        tick2 = compute_tick_health(phase2)
        zones2 = compute_zone_health(phase2)
        anomalies2 = detect_anomalies(phase2)
        status2 = determine_status(tick2, zones2, anomalies2)
        assert status2 == "critical"

        # Phase 3: Recovery (ticks 16-25)
        phase3 = [
            e
            for e in entries
            if e.component == "game_loop" and 16 <= e.data.get("tick", 0) <= 25
        ]
        tick3 = compute_tick_health(phase3)
        anomalies3 = detect_anomalies(phase3)
        status3 = determine_status(tick3, [], anomalies3)
        assert status3 == "healthy"


class TestZoneCrashDetectedInHealthReport:
    """build_health_report -> critical, CRASHED zone, zone_crash anomaly."""

    def test_zone_crash_report(
        self, zone_crash_log: Path, mock_game_server: dict
    ) -> None:
        host, port = mock_game_server["host"], mock_game_server["port"]
        report = build_health_report(
            log_path=zone_crash_log,
            game_host=host,
            game_port=port,
            skip_faults=True,
        )
        assert report.status == "critical"
        crashed_zones = [z for z in report.zones if z.state == "CRASHED"]
        assert len(crashed_zones) >= 1
        zone_crash_anomalies = [
            a for a in report.anomalies if a.type == "zone_crash"
        ]
        assert len(zone_crash_anomalies) >= 1


class TestFaultListAndStatusQueryComposition:
    """list_all_faults -> fault_status -> deactivate_all all succeed."""

    def test_list_status_deactivate_composition(
        self, mock_control_server: dict
    ) -> None:
        host = mock_control_server["host"]
        port = mock_control_server["port"]

        # List all faults
        list_resp = list_all_faults(host, port)
        assert list_resp.success is True
        assert list_resp.faults is not None
        assert len(list_resp.faults) >= 1

        # Query status of first fault
        first_fault_id = list_resp.faults[0].id
        status_resp = fault_status(host, port, first_fault_id)
        assert status_resp.success is True

        # Deactivate all
        deactivate_resp = deactivate_all_faults(host, port)
        assert deactivate_resp.success is True
