#pragma once

#include <cstdint>
#include <unordered_map>

namespace wow {

/// Distinguishes player-controlled entities from server-controlled NPCs.
enum class EntityType { PLAYER, NPC };

/// 3D position in world space (matches WoW's coordinate system).
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

bool operator==(const Position& a, const Position& b);
bool operator!=(const Position& a, const Position& b);

/// Euclidean distance between two positions.
float distance(const Position& a, const Position& b);

/// Per-entity combat state, owned by the game thread alongside Position and CastState.
///
/// Tracks health, mitigation stats, alive/dead status, NPC auto-attack damage,
/// and a per-entity threat table (WoW model: each mob tracks its own threat list).
struct CombatState {
    int32_t health = 100;              ///< Current health points
    int32_t max_health = 100;          ///< Maximum health points
    float armor = 0.0f;               ///< Physical damage mitigation, 0.0–0.75
    float resistance = 0.0f;          ///< Magical damage mitigation, 0.0–0.75
    bool is_alive = true;             ///< Whether the entity is alive
    int32_t base_attack_damage = 0;   ///< NPC auto-attack damage per tick (0 for players)
    std::unordered_map<uint64_t, float> threat_table;  ///< attacker_id → accumulated threat
};

/// Per-entity spell casting state, owned by the game thread alongside Position.
///
/// Tracks active cast progress, GCD expiry, and the movement-cancels-cast flag
/// set by MovementProcessor and consumed by SpellCastProcessor each tick.
struct CastState {
    bool is_casting = false;           ///< Whether a spell is being channeled
    uint32_t spell_id = 0;            ///< ID of the spell being cast (0 = none)
    uint32_t cast_ticks_remaining = 0; ///< Ticks left until cast completes
    uint64_t gcd_expires_tick = 0;     ///< Absolute tick when GCD expires (0 = no GCD)
    bool moved_this_tick = false;      ///< Set by MovementProcessor, consumed by SpellCastProcessor
};

/// Represents a player or NPC's in-world avatar.
///
/// Keyed by session_id (players) or NPC ID (NPCs) in the entity map.
/// EntityType distinguishes player-controlled from server-controlled entities.
class Entity {
public:
    /// Construct an entity for the given session/NPC ID, at the world origin.
    explicit Entity(uint64_t session_id, EntityType type = EntityType::PLAYER);

    /// The session/NPC ID that identifies this entity.
    uint64_t session_id() const;

    /// The entity type (PLAYER or NPC).
    EntityType entity_type() const;

    /// Current position in world space.
    const Position& position() const;

    /// Update position (called by MovementProcessor).
    void set_position(const Position& pos);

    /// Mutable access to spell casting state (used by SpellCastProcessor).
    CastState& cast_state();

    /// Const access to spell casting state (used for inspection/telemetry).
    const CastState& cast_state() const;

    /// Mutable access to combat state (used by CombatProcessor).
    CombatState& combat_state();

    /// Const access to combat state (used for inspection/telemetry).
    const CombatState& combat_state() const;

private:
    uint64_t session_id_;
    EntityType entity_type_;
    Position position_;
    CastState cast_state_;
    CombatState combat_state_;
};

}  // namespace wow
