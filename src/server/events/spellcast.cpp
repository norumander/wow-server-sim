#include "server/events/spellcast.h"

#include <string>

#include "server/telemetry/logger.h"

namespace wow {

// ---------------------------------------------------------------------------
// SpellCastEvent
// ---------------------------------------------------------------------------

SpellCastEvent::SpellCastEvent(uint64_t session_id, SpellAction action,
                               uint32_t spell_id, uint32_t cast_time_ticks)
    : GameEvent(EventType::SPELL_CAST, session_id)
    , action_(action)
    , spell_id_(spell_id)
    , cast_time_ticks_(cast_time_ticks)
{
}

SpellAction SpellCastEvent::action() const
{
    return action_;
}

uint32_t SpellCastEvent::spell_id() const
{
    return spell_id_;
}

uint32_t SpellCastEvent::cast_time_ticks() const
{
    return cast_time_ticks_;
}

// ---------------------------------------------------------------------------
// SpellCastProcessor
// ---------------------------------------------------------------------------

SpellCastResult SpellCastProcessor::process(
    std::vector<std::unique_ptr<GameEvent>>& events,
    std::unordered_map<uint64_t, Entity>& entities,
    uint64_t current_tick)
{
    SpellCastResult result;

    // Step 1: Movement cancellation — if moved_this_tick && is_casting: cancel.
    for (auto& [sid, entity] : entities) {
        auto& cs = entity.cast_state();
        if (cs.moved_this_tick && cs.is_casting) {
            uint32_t cancelled_spell = cs.spell_id;
            cs.is_casting = false;
            cs.spell_id = 0;
            cs.cast_ticks_remaining = 0;
            ++result.casts_interrupted;

            if (Logger::is_initialized()) {
                Logger::instance().event("spellcast", "Cast interrupted", {
                    {"session_id", sid},
                    {"spell_id", cancelled_spell},
                    {"reason", "movement"},
                });
            }
        }
    }

    // Step 2: Interrupt events — INTERRUPT events cancel targeted casts.
    for (auto& event : events) {
        if (!event || event->event_type() != EventType::SPELL_CAST) {
            continue;
        }

        auto* spell_event = static_cast<SpellCastEvent*>(event.get());
        if (spell_event->action() != SpellAction::INTERRUPT) {
            continue;
        }

        uint64_t sid = spell_event->session_id();
        auto it = entities.find(sid);
        if (it == entities.end()) {
            continue;
        }

        auto& cs = it->second.cast_state();
        if (!cs.is_casting) {
            continue;
        }

        uint32_t cancelled_spell = cs.spell_id;
        cs.is_casting = false;
        cs.spell_id = 0;
        cs.cast_ticks_remaining = 0;
        ++result.casts_interrupted;

        if (Logger::is_initialized()) {
            Logger::instance().event("spellcast", "Cast interrupted", {
                {"session_id", sid},
                {"spell_id", cancelled_spell},
                {"reason", "interrupt"},
            });
        }
    }

    // Step 3: Advance timers — decrement cast_ticks_remaining; complete if 0.
    for (auto& [sid, entity] : entities) {
        auto& cs = entity.cast_state();
        if (!cs.is_casting) {
            continue;
        }

        --cs.cast_ticks_remaining;
        if (cs.cast_ticks_remaining == 0) {
            uint32_t completed_spell = cs.spell_id;
            cs.is_casting = false;
            cs.spell_id = 0;
            ++result.casts_completed;

            if (Logger::is_initialized()) {
                Logger::instance().event("spellcast", "Cast completed", {
                    {"session_id", sid},
                    {"spell_id", completed_spell},
                });
            }
        }
    }

    // Step 4: Process CAST_START events (GCD check, initiate cast).
    for (auto& event : events) {
        if (!event || event->event_type() != EventType::SPELL_CAST) {
            continue;
        }

        auto* spell_event = static_cast<SpellCastEvent*>(event.get());
        if (spell_event->action() != SpellAction::CAST_START) {
            continue;
        }

        uint64_t sid = spell_event->session_id();
        auto it = entities.find(sid);
        if (it == entities.end()) {
            if (Logger::is_initialized()) {
                Logger::instance().error("spellcast", "Unknown session for spell cast event", {
                    {"session_id", sid},
                });
            }
            continue;
        }

        auto& cs = it->second.cast_state();

        // GCD check: gcd_expires_tick > current_tick means GCD is active.
        if (cs.gcd_expires_tick > current_tick) {
            ++result.gcd_blocked;
            if (Logger::is_initialized()) {
                Logger::instance().event("spellcast", "Cast blocked by GCD", {
                    {"session_id", sid},
                    {"spell_id", spell_event->spell_id()},
                    {"gcd_expires_tick", cs.gcd_expires_tick},
                    {"current_tick", current_tick},
                });
            }
            continue;
        }

        // Set GCD (triggers on cast start, not completion — matches WoW).
        cs.gcd_expires_tick = current_tick + kGlobalCooldownTicks;

        // Instant cast (cast_time_ticks == 0): start + complete same tick.
        if (spell_event->cast_time_ticks() == 0) {
            ++result.casts_started;
            ++result.casts_completed;

            if (Logger::is_initialized()) {
                Logger::instance().event("spellcast", "Cast started", {
                    {"session_id", sid},
                    {"spell_id", spell_event->spell_id()},
                    {"cast_time_ticks", 0},
                    {"instant", true},
                });
                Logger::instance().event("spellcast", "Cast completed", {
                    {"session_id", sid},
                    {"spell_id", spell_event->spell_id()},
                });
            }
            continue;
        }

        // Normal cast: set casting state.
        cs.is_casting = true;
        cs.spell_id = spell_event->spell_id();
        cs.cast_ticks_remaining = spell_event->cast_time_ticks();
        ++result.casts_started;

        if (Logger::is_initialized()) {
            Logger::instance().event("spellcast", "Cast started", {
                {"session_id", sid},
                {"spell_id", spell_event->spell_id()},
                {"cast_time_ticks", spell_event->cast_time_ticks()},
            });
        }
    }

    // Step 5: Clear moved_this_tick flags on all entities.
    for (auto& [sid, entity] : entities) {
        entity.cast_state().moved_this_tick = false;
    }

    return result;
}

}  // namespace wow
