"""Performance benchmark suite for the WoW server simulator.

Orchestrates scaling tests by composing mock_client, health_check,
and percentile computation into automated benchmark scenarios with
pass/fail evaluation against configurable thresholds.
"""

from __future__ import annotations
