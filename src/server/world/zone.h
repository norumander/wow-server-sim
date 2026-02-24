#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "server/events/combat.h"
#include "server/events/event_queue.h"
#include "server/events/movement.h"
#include "server/events/spellcast.h"
#include "server/world/entity.h"

namespace wow {

/// Unique identifier for a zone instance.
using ZoneId = uint32_t;

/// Sentinel value indicating no zone assignment.
constexpr ZoneId kNoZone = 0;

/// Runtime state of a zone, visible in health telemetry.
enum class ZoneState {
    ACTIVE,    ///< Normal operation
    DEGRADED,  ///< Recovering from a crash (one successful tick since crash)
    CRASHED,   ///< Exception occurred during last tick
};

/// Configuration for creating a zone.
struct ZoneConfig {
    ZoneId zone_id = 0;     ///< Unique zone identifier
    std::string name;       ///< Human-readable zone name (e.g. "Elwynn Forest")
};

/// Result of a single Zone::tick() invocation, for telemetry and testing.
struct ZoneTickResult {
    ZoneId zone_id = 0;             ///< Which zone was ticked
    uint64_t tick = 0;              ///< The tick number
    size_t events_processed = 0;    ///< Events drained from queue
    size_t entities_moved = 0;      ///< Entities whose positions changed
    SpellCastResult spell_result;   ///< Spell cast phase results
    CombatResult combat_result;     ///< Combat phase results
    double duration_ms = 0.0;       ///< Wall-clock time for this tick
    bool had_error = false;         ///< Whether an exception was caught
    std::string error_message;      ///< Exception message (empty if no error)
};

/// Health snapshot for a zone, used by telemetry and monitoring.
struct ZoneHealth {
    ZoneId zone_id = 0;
    ZoneState state = ZoneState::ACTIVE;
    uint64_t total_ticks = 0;
    uint64_t error_count = 0;
    size_t entity_count = 0;
    size_t event_queue_depth = 0;
    double last_tick_duration_ms = 0.0;
};

/// Self-contained processing unit with exception guard.
///
/// Each Zone owns its entity map, per-zone EventQueue, and processor instances
/// (MovementProcessor, SpellCastProcessor, CombatProcessor). tick() drains the
/// queue and runs the full pipeline inside a try/catch exception guard.
///
/// State recovery arc: CRASHED -> DEGRADED -> ACTIVE on successive successful
/// ticks, visible in telemetry for observability demonstrations.
class Zone {
public:
    /// Construct a zone with the given configuration.
    explicit Zone(const ZoneConfig& config);

    /// The zone's unique identifier.
    ZoneId zone_id() const;

    /// The zone's human-readable name.
    std::string_view name() const;

    /// Current runtime state (ACTIVE, DEGRADED, or CRASHED).
    ZoneState state() const;

    // --- Entity management ---

    /// Add an entity to this zone. Returns false if session_id already present.
    bool add_entity(Entity entity);

    /// Remove an entity by session_id. Returns false if not found.
    bool remove_entity(uint64_t session_id);

    /// Remove and return an entity for zone transfer. Preserves entity state.
    std::optional<Entity> take_entity(uint64_t session_id);

    /// Check if an entity with the given session_id is in this zone.
    bool has_entity(uint64_t session_id) const;

    /// Number of entities currently in this zone.
    size_t entity_count() const;

    /// Const access to the entity map (for inspection/testing).
    const std::unordered_map<uint64_t, Entity>& entities() const;

    // --- Event delivery (thread-safe via EventQueue) ---

    /// Push an event into this zone's queue (thread-safe).
    void push_event(std::unique_ptr<GameEvent> event);

    /// Current event queue depth (thread-safe).
    size_t event_queue_depth() const;

    // --- Tick pipeline ---

    /// Execute one tick: drain queue -> Movement -> SpellCast -> Combat.
    /// Wrapped in try/catch exception guard for crash isolation.
    ZoneTickResult tick(uint64_t current_tick);

    // --- Fault injection hooks ---

    /// Set a hook called before tick processing (for fault injection/testing).
    void set_pre_tick_hook(std::function<void()> hook);

    /// Set a hook called after tick processing (for fault injection/testing).
    void set_post_tick_hook(std::function<void()> hook);

    // --- Health ---

    /// Snapshot of this zone's current health metrics.
    ZoneHealth health() const;

private:
    ZoneConfig config_;
    ZoneState state_ = ZoneState::ACTIVE;
    uint64_t total_ticks_ = 0;
    uint64_t error_count_ = 0;
    double last_tick_duration_ms_ = 0.0;

    std::unordered_map<uint64_t, Entity> entities_;
    EventQueue event_queue_;
    MovementProcessor movement_processor_;
    SpellCastProcessor spellcast_processor_;
    CombatProcessor combat_processor_;
    std::function<void()> pre_tick_hook_;
    std::function<void()> post_tick_hook_;
};

}  // namespace wow
