"""Tests for wowsim.log_parser — telemetry parsing, filtering, summarizing, anomaly detection."""

from datetime import datetime, timezone
from io import StringIO
from pathlib import Path

from wowsim.log_parser import (
    detect_anomalies,
    filter_entries,
    parse_file,
    parse_line,
    parse_stream,
    summarize,
)


# ============================================================
# Group A: TelemetryEntry Model & Line Parsing (3 tests)
# ============================================================


class TestParseLine:
    """Tests for parse_line() — single JSONL line → TelemetryEntry."""

    def test_parse_valid_entry(self, sample_metric_line: str) -> None:
        """Valid JSONL line produces a TelemetryEntry with correct fields."""
        entry = parse_line(sample_metric_line)
        assert entry is not None
        assert entry.v == 1
        assert entry.type == "metric"
        assert entry.component == "game_loop"
        assert entry.message == "Tick completed"
        assert entry.data["tick"] == 42
        assert entry.data["duration_ms"] == 3.5
        assert isinstance(entry.timestamp, datetime)

    def test_parse_entry_without_data(self, sample_entry_no_data_line: str) -> None:
        """Entry with no data field parses successfully with empty dict default."""
        entry = parse_line(sample_entry_no_data_line)
        assert entry is not None
        assert entry.data == {}
        assert entry.component == "server"
        assert entry.message == "Server shutting down"

    def test_parse_invalid_json_returns_none(self) -> None:
        """Malformed JSON line returns None instead of raising."""
        assert parse_line("NOT VALID JSON") is None
        assert parse_line("") is None
        assert parse_line('{"v": 1}') is None  # missing required fields


# ============================================================
# Group B: File/Stream Parsing (3 tests)
# ============================================================


class TestFileParsing:
    """Tests for parse_file() and parse_stream()."""

    def test_parse_file_reads_all_lines(self, sample_log_file: Path) -> None:
        """Temp file with 3 valid JSONL lines produces 3 entries."""
        entries = parse_file(sample_log_file)
        assert len(entries) == 3
        assert entries[0].component == "game_loop"
        assert entries[1].component == "game_server"
        assert entries[2].component == "zone"

    def test_parse_file_skips_invalid_lines(
        self, sample_log_file_with_invalid: Path
    ) -> None:
        """File with mixed valid/invalid lines returns only valid entries."""
        entries = parse_file(sample_log_file_with_invalid)
        assert len(entries) == 2
        assert all(e.component == "game_loop" for e in entries)

    def test_parse_stream_from_stringio(self, sample_jsonl: str) -> None:
        """StringIO input produces the same entries as file parsing."""
        stream = StringIO(sample_jsonl)
        entries = parse_stream(stream)
        assert len(entries) == 3
        assert entries[0].type == "metric"
        assert entries[1].type == "event"
        assert entries[2].type == "error"


# ============================================================
# Group C: Filtering (4 tests)
# ============================================================


class TestFiltering:
    """Tests for filter_entries()."""

    def test_filter_by_type(self, sample_log_file: Path) -> None:
        """Filtering by type='error' returns only error entries."""
        entries = parse_file(sample_log_file)
        filtered = filter_entries(entries, type_filter="error")
        assert len(filtered) == 1
        assert filtered[0].type == "error"

    def test_filter_by_component(self, sample_log_file: Path) -> None:
        """Filtering by component='zone' returns only zone entries."""
        entries = parse_file(sample_log_file)
        filtered = filter_entries(entries, component_filter="zone")
        assert len(filtered) == 1
        assert filtered[0].component == "zone"

    def test_filter_by_message(self, sample_log_file: Path) -> None:
        """Filtering by message substring 'Tick' returns matching entries."""
        entries = parse_file(sample_log_file)
        filtered = filter_entries(entries, message_filter="Tick")
        assert len(filtered) == 1
        assert "Tick" in filtered[0].message

    def test_filter_combined(self, sample_log_file: Path) -> None:
        """Combining type + component filters applies both."""
        entries = parse_file(sample_log_file)
        # Only the metric from game_loop matches both
        filtered = filter_entries(
            entries, type_filter="metric", component_filter="game_loop"
        )
        assert len(filtered) == 1
        assert filtered[0].type == "metric"
        assert filtered[0].component == "game_loop"


# ============================================================
# Group D: Summary (3 tests)
# ============================================================


class TestSummarize:
    """Tests for summarize()."""

    def test_summarize_counts_by_type(self, sample_log_file: Path) -> None:
        """Summary correctly counts entries by type."""
        entries = parse_file(sample_log_file)
        summary = summarize(entries)
        assert summary.total_entries == 3
        assert summary.entries_by_type["metric"] == 1
        assert summary.entries_by_type["event"] == 1
        assert summary.entries_by_type["error"] == 1
        assert summary.error_count == 1

    def test_summarize_counts_by_component(self, sample_log_file: Path) -> None:
        """Summary correctly counts entries by component."""
        entries = parse_file(sample_log_file)
        summary = summarize(entries)
        assert summary.entries_by_component["game_loop"] == 1
        assert summary.entries_by_component["game_server"] == 1
        assert summary.entries_by_component["zone"] == 1

    def test_summarize_time_range(self, sample_log_file: Path) -> None:
        """Summary computes start, end, and duration from timestamps."""
        entries = parse_file(sample_log_file)
        summary = summarize(entries)
        assert summary.time_range_start is not None
        assert summary.time_range_end is not None
        # All sample entries use the same timestamp, so duration is 0
        assert summary.duration_seconds >= 0.0


# ============================================================
# Group E: Anomaly Detection (5 tests)
# ============================================================


class TestAnomalyDetection:
    """Tests for detect_anomalies()."""

    def test_detect_latency_spike_warning(
        self, entries_with_anomalies: list[str]
    ) -> None:
        """Tick with duration_ms=70 detected as warning latency spike."""
        entries = [parse_line(line) for line in entries_with_anomalies]
        entries = [e for e in entries if e is not None]
        anomalies = detect_anomalies(entries)
        warnings = [
            a
            for a in anomalies
            if a.type == "latency_spike" and a.severity == "warning"
        ]
        assert len(warnings) >= 1
        assert "70" in warnings[0].message or "70.0" in warnings[0].message

    def test_detect_latency_spike_critical(
        self, entries_with_anomalies: list[str]
    ) -> None:
        """Tick with duration_ms=150 detected as critical latency spike."""
        entries = [parse_line(line) for line in entries_with_anomalies]
        entries = [e for e in entries if e is not None]
        anomalies = detect_anomalies(entries)
        criticals = [
            a
            for a in anomalies
            if a.type == "latency_spike" and a.severity == "critical"
        ]
        assert len(criticals) >= 1
        assert "150" in criticals[0].message or "150.0" in criticals[0].message

    def test_detect_zone_crash(self, entries_with_anomalies: list[str]) -> None:
        """Zone tick exception detected as critical zone_crash anomaly."""
        entries = [parse_line(line) for line in entries_with_anomalies]
        entries = [e for e in entries if e is not None]
        anomalies = detect_anomalies(entries)
        zone_crashes = [a for a in anomalies if a.type == "zone_crash"]
        assert len(zone_crashes) >= 1
        assert zone_crashes[0].severity == "critical"

    def test_detect_error_burst(self, entries_with_anomalies: list[str]) -> None:
        """5+ errors within 10 seconds detected as critical error_burst."""
        entries = [parse_line(line) for line in entries_with_anomalies]
        entries = [e for e in entries if e is not None]
        anomalies = detect_anomalies(entries)
        bursts = [a for a in anomalies if a.type == "error_burst"]
        assert len(bursts) >= 1
        assert bursts[0].severity == "critical"

    def test_detect_no_anomalies_clean_log(self, sample_metric_line: str) -> None:
        """Healthy entries produce no anomalies."""
        entry = parse_line(sample_metric_line)
        assert entry is not None
        anomalies = detect_anomalies([entry])
        assert anomalies == []
