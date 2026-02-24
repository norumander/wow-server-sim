#include "server/events/spellcast.h"

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
// SpellCastProcessor — stubs (TDD RED phase)
// ---------------------------------------------------------------------------

SpellCastResult SpellCastProcessor::process(
    std::vector<std::unique_ptr<GameEvent>>& /*events*/,
    std::unordered_map<uint64_t, Entity>& /*entities*/,
    uint64_t /*current_tick*/)
{
    return SpellCastResult{};
}

}  // namespace wow
