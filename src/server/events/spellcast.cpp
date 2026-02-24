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

    // Phase 4: Process CAST_START events (GCD check, initiate cast).
    for (auto& event : events) {
        if (!event || event->event_type() != EventType::SPELL_CAST) {
            continue;
        }

        auto* spell_event = static_cast<SpellCastEvent*>(event.get());
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

        if (spell_event->action() != SpellAction::CAST_START) {
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

    return result;
}

}  // namespace wow
