#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "server/world/zone.h"

namespace wow {

/// Aggregated results from ticking all zones in a ZoneManager.
struct ZoneManagerTickResult {
    uint64_t tick = 0;              ///< The tick number
    size_t zones_ticked = 0;        ///< Number of zones that were ticked
    size_t total_events = 0;        ///< Sum of events processed across all zones
    size_t zones_with_errors = 0;   ///< Number of zones that had exceptions
    std::vector<ZoneTickResult> zone_results;  ///< Per-zone results
};

/// Hub-and-spoke coordinator for all Zone instances.
///
/// Maintains session-to-zone mapping, routes events from intake to per-zone
/// queues, and ticks all zones sequentially (MVP). Separate from Zone because:
/// - Event routing is a cross-zone concern
/// - Session transfer is a two-zone operation
/// - Sequential tick orchestration with per-zone error reporting
class ZoneManager {
public:
    /// Create a new zone with the given configuration. Returns the zone ID.
    ZoneId create_zone(const ZoneConfig& config);

    /// Get a zone by ID. Returns nullptr if not found.
    Zone* get_zone(ZoneId zone_id);

    /// Get a zone by ID (const). Returns nullptr if not found.
    const Zone* get_zone(ZoneId zone_id) const;

    /// Number of zones managed.
    size_t zone_count() const;

    /// Assign a session to a zone, creating an Entity in the target zone.
    /// Returns false if zone doesn't exist or session already assigned.
    bool assign_session(uint64_t session_id, ZoneId zone_id);

    /// Remove a session from its assigned zone.
    /// Returns false if session is not assigned.
    bool remove_session(uint64_t session_id);

    /// Transfer a session from its current zone to a target zone.
    /// Preserves entity state (position, combat, cast).
    /// Returns false if session not assigned or target zone doesn't exist.
    bool transfer_session(uint64_t session_id, ZoneId target_zone_id);

    /// Look up which zone a session is assigned to.
    /// Returns kNoZone if not assigned.
    ZoneId session_zone(uint64_t session_id) const;

    /// Route events from an intake vector to per-zone queues by session_id.
    /// Events for unassigned sessions are discarded. Returns number routed.
    size_t route_events(std::vector<std::unique_ptr<GameEvent>>& events);

    /// Tick all zones sequentially. Per-zone errors are isolated.
    ZoneManagerTickResult tick_all(uint64_t current_tick);

private:
    std::unordered_map<ZoneId, std::unique_ptr<Zone>> zones_;
    std::unordered_map<uint64_t, ZoneId> session_zone_map_;
};

}  // namespace wow
