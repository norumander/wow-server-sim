"""Tests for wowsim.log_parser — telemetry parsing, filtering, summarizing, anomaly detection."""

from datetime import datetime, timezone

from wowsim.log_parser import parse_line


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
