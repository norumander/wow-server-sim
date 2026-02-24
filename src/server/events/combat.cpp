#include "server/events/combat.h"

#include "server/telemetry/logger.h"

namespace wow {

// ---------------------------------------------------------------------------
// CombatEvent
// ---------------------------------------------------------------------------

CombatEvent::CombatEvent(uint64_t session_id, CombatAction action,
                         uint64_t target_session_id, int32_t base_damage,
                         DamageType damage_type)
    : GameEvent(EventType::COMBAT, session_id)
    , action_(action)
    , target_session_id_(target_session_id)
    , base_damage_(base_damage)
    , damage_type_(damage_type)
{
}

CombatAction CombatEvent::action() const
{
    return action_;
}

uint64_t CombatEvent::target_session_id() const
{
    return target_session_id_;
}

int32_t CombatEvent::base_damage() const
{
    return base_damage_;
}

DamageType CombatEvent::damage_type() const
{
    return damage_type_;
}

// ---------------------------------------------------------------------------
// CombatProcessor
// ---------------------------------------------------------------------------

CombatResult CombatProcessor::process(
    std::vector<std::unique_ptr<GameEvent>>& /*events*/,
    std::unordered_map<uint64_t, Entity>& /*entities*/)
{
    CombatResult result;
    // Stub — tests for Groups C–H will fail until implemented.
    return result;
}

}  // namespace wow
