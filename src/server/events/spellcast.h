#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "server/events/event.h"
#include "server/world/entity.h"

namespace wow {

/// WoW global cooldown: 1.5 seconds at 20 Hz = 30 ticks.
constexpr uint32_t kGlobalCooldownTicks = 30;

/// Actions carried by a SpellCastEvent.
enum class SpellAction {
    CAST_START,  ///< Initiate a new spell cast
    INTERRUPT,   ///< Cancel the caster's active spell
};

/// Spell cast event, processed during the SpellCastPhase of the tick pipeline.
///
/// CAST_START carries spell_id and cast_time_ticks (0 = instant cast).
/// INTERRUPT cancels whatever spell the originating session is casting.
class SpellCastEvent : public GameEvent {
public:
    /// Construct a CAST_START event.
    SpellCastEvent(uint64_t session_id, SpellAction action, uint32_t spell_id,
                   uint32_t cast_time_ticks = 0);

    /// The spell action (CAST_START or INTERRUPT).
    SpellAction action() const;

    /// The spell ID for CAST_START events.
    uint32_t spell_id() const;

    /// The cast time in ticks for CAST_START events (0 = instant).
    uint32_t cast_time_ticks() const;

private:
    SpellAction action_;
    uint32_t spell_id_;
    uint32_t cast_time_ticks_;
};

/// Result of SpellCastProcessor::process() for telemetry and testing.
struct SpellCastResult {
    size_t casts_started = 0;     ///< New casts initiated this tick
    size_t casts_completed = 0;   ///< Casts that finished (timer reached 0)
    size_t casts_interrupted = 0; ///< Casts cancelled (by event or movement)
    size_t gcd_blocked = 0;       ///< Cast attempts rejected by GCD
};

/// Processes spell cast events during the SpellCastPhase of the tick pipeline.
///
/// Processing order within one tick:
///   1. Movement cancellation — if moved_this_tick && is_casting: cancel
///   2. Interrupt events — INTERRUPT events cancel targeted casts
///   3. Advance timers — decrement cast_ticks_remaining; complete if 0
///   4. Process CAST_START — GCD check, already-casting check, initiate
///   5. Clear moved_this_tick flags on all entities
class SpellCastProcessor {
public:
    /// Process all spell cast events for this tick.
    ///
    /// @param events       All events for this tick (spell events consumed).
    /// @param entities     Map of session_id -> Entity for state updates.
    /// @param current_tick The current game tick number (for GCD calculation).
    /// @return Aggregated result counts for telemetry/testing.
    SpellCastResult process(std::vector<std::unique_ptr<GameEvent>>& events,
                            std::unordered_map<uint64_t, Entity>& entities,
                            uint64_t current_tick);
};

}  // namespace wow
