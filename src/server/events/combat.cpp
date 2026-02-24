#include "server/events/combat.h"

#include <algorithm>
#include <cmath>
#include <string>

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

namespace {

/// Convert DamageType to string for telemetry.
std::string damage_type_to_string(DamageType type)
{
    switch (type) {
    case DamageType::PHYSICAL: return "physical";
    case DamageType::MAGICAL:  return "magical";
    }
    return "unknown";
}

/// Compute mitigated damage: base_damage * (1 - clamp(mitigation, 0, 0.75)).
int32_t compute_actual_damage(int32_t base_damage, float mitigation)
{
    float clamped = std::clamp(mitigation, 0.0f, kMaxMitigation);
    return static_cast<int32_t>(std::round(
        static_cast<float>(base_damage) * (1.0f - clamped)));
}

/// Select the mitigation stat based on damage type.
float get_mitigation(const CombatState& target_state, DamageType type)
{
    return type == DamageType::PHYSICAL ? target_state.armor : target_state.resistance;
}

/// Apply damage to target, handle death, emit telemetry. Returns actual damage dealt.
int32_t apply_damage(uint64_t attacker_id, uint64_t target_id,
                     int32_t base_damage, DamageType damage_type,
                     Entity& target, CombatResult& result)
{
    float mitigation = get_mitigation(target.combat_state(), damage_type);
    int32_t actual_damage = compute_actual_damage(base_damage, mitigation);

    target.combat_state().health -= actual_damage;
    result.total_damage_dealt += actual_damage;

    // Threat: damage dealt = threat generated (ADR-012).
    target.combat_state().threat_table[attacker_id] += static_cast<float>(actual_damage);

    if (Logger::is_initialized()) {
        Logger::instance().event("combat", "Damage dealt", {
            {"attacker_id", attacker_id},
            {"target_id", target_id},
            {"base_damage", base_damage},
            {"actual_damage", actual_damage},
            {"damage_type", damage_type_to_string(damage_type)},
            {"mitigation", mitigation},
            {"target_health", target.combat_state().health},
        });
    }

    // Inline death check — prevents double-damage to newly-dead entities.
    if (target.combat_state().health <= 0) {
        target.combat_state().is_alive = false;
        ++result.kills;

        if (Logger::is_initialized()) {
            Logger::instance().event("combat", "Entity killed", {
                {"target_id", target_id},
                {"killer_id", attacker_id},
            });
        }
    }

    return actual_damage;
}

}  // anonymous namespace

CombatResult CombatProcessor::process(
    std::vector<std::unique_ptr<GameEvent>>& events,
    std::unordered_map<uint64_t, Entity>& entities)
{
    CombatResult result;

    // Step 1: Process ATTACK events.
    for (auto& event : events) {
        if (!event || event->event_type() != EventType::COMBAT) {
            continue;
        }

        auto* combat_event = static_cast<CombatEvent*>(event.get());
        if (combat_event->action() != CombatAction::ATTACK) {
            continue;
        }

        uint64_t attacker_id = combat_event->session_id();
        uint64_t target_id = combat_event->target_session_id();

        // Validate attacker exists and is alive.
        auto attacker_it = entities.find(attacker_id);
        if (attacker_it == entities.end() || !attacker_it->second.combat_state().is_alive) {
            ++result.attacks_missed;
            continue;
        }

        // Validate target exists and is alive.
        auto target_it = entities.find(target_id);
        if (target_it == entities.end() || !target_it->second.combat_state().is_alive) {
            ++result.attacks_missed;
            continue;
        }

        apply_damage(attacker_id, target_id,
                     combat_event->base_damage(), combat_event->damage_type(),
                     target_it->second, result);
        ++result.attacks_processed;
    }

    // Step 2: NPC auto-attack — each living NPC attacks highest-threat target.
    for (auto& [npc_id, npc_entity] : entities) {
        if (npc_entity.entity_type() != EntityType::NPC) {
            continue;
        }
        if (!npc_entity.combat_state().is_alive) {
            continue;
        }
        if (npc_entity.combat_state().base_attack_damage <= 0) {
            continue;
        }

        const auto& threat = npc_entity.combat_state().threat_table;
        if (threat.empty()) {
            continue;
        }

        // Find highest-threat living target.
        uint64_t best_target = 0;
        float best_threat = -1.0f;
        for (const auto& [tid, tval] : threat) {
            auto it = entities.find(tid);
            if (it != entities.end() && it->second.combat_state().is_alive && tval > best_threat) {
                best_target = tid;
                best_threat = tval;
            }
        }

        if (best_threat < 0.0f) {
            continue;  // no living threat targets
        }

        auto target_it = entities.find(best_target);
        if (target_it == entities.end()) {
            continue;
        }

        apply_damage(npc_id, best_target,
                     npc_entity.combat_state().base_attack_damage,
                     DamageType::PHYSICAL, target_it->second, result);
        ++result.npc_attacks;
    }

    // Step 3: Clean up threat tables — remove dead entity IDs.
    for (auto& [eid, entity] : entities) {
        if (!entity.combat_state().is_alive) {
            continue;
        }
        auto& threat = entity.combat_state().threat_table;
        for (auto it = threat.begin(); it != threat.end(); ) {
            auto dead_it = entities.find(it->first);
            if (dead_it != entities.end() && !dead_it->second.combat_state().is_alive) {
                it = threat.erase(it);
            } else {
                ++it;
            }
        }
    }

    return result;
}

}  // namespace wow
