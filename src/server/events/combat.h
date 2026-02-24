#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "server/events/event.h"
#include "server/world/entity.h"

namespace wow {

/// Maximum damage mitigation from armor or resistance (75%).
constexpr float kMaxMitigation = 0.75f;

/// Default starting health for entities.
constexpr int32_t kDefaultHealth = 100;

/// Default maximum health for entities.
constexpr int32_t kDefaultMaxHealth = 100;

/// Damage type determines which mitigation stat applies.
enum class DamageType {
    PHYSICAL,  ///< Mitigated by armor
    MAGICAL,   ///< Mitigated by resistance
};

/// Combat actions carried by a CombatEvent.
enum class CombatAction {
    ATTACK,  ///< Deal damage to a target (MVP only action)
};

/// Combat event, processed during the CombatPhase of the tick pipeline.
///
/// session_id() (inherited) = attacker, target_session_id() = defender.
/// DamageType selects mitigation: PHYSICAL → armor, MAGICAL → resistance.
class CombatEvent : public GameEvent {
public:
    /// Construct a combat event.
    CombatEvent(uint64_t session_id, CombatAction action,
                uint64_t target_session_id, int32_t base_damage,
                DamageType damage_type);

    /// The combat action (ATTACK for MVP).
    CombatAction action() const;

    /// The entity being attacked.
    uint64_t target_session_id() const;

    /// Base damage before mitigation.
    int32_t base_damage() const;

    /// Whether this is physical or magical damage.
    DamageType damage_type() const;

private:
    CombatAction action_;
    uint64_t target_session_id_;
    int32_t base_damage_;
    DamageType damage_type_;
};

/// Result of CombatProcessor::process() for telemetry and testing.
struct CombatResult {
    size_t attacks_processed = 0;   ///< Attacks that dealt damage
    size_t attacks_missed = 0;      ///< Attacks skipped (invalid attacker/target/dead)
    size_t kills = 0;               ///< Entities killed this tick
    size_t npc_attacks = 0;         ///< NPC auto-attacks executed
    int64_t total_damage_dealt = 0; ///< Sum of all actual damage applied
};

/// Processes combat events during the CombatPhase of the tick pipeline.
///
/// Processing order within one tick:
///   1. Process ATTACK events — validate, mitigate, apply damage, threat, death check
///   2. NPC auto-attack — each living NPC attacks highest-threat target
///   3. Clean up threat tables — remove dead entity IDs from all living entities' tables
class CombatProcessor {
public:
    /// Process all combat events for this tick.
    ///
    /// @param events   All events for this tick (combat events consumed).
    /// @param entities Map of session_id/npc_id -> Entity for state updates.
    /// @return Aggregated result counts for telemetry/testing.
    CombatResult process(std::vector<std::unique_ptr<GameEvent>>& events,
                         std::unordered_map<uint64_t, Entity>& entities);
};

}  // namespace wow
