#pragma once

#include <cstdint>

namespace wow {

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

/// Represents a player's in-world avatar.
///
/// Keyed by session_id for MVP — one entity per connected player.
/// NPCs with independent entity IDs deferred to Step 8 (combat).
class Entity {
public:
    /// Construct an entity for the given session, at the world origin.
    explicit Entity(uint64_t session_id);

    /// The session ID that owns this entity.
    uint64_t session_id() const;

    /// Current position in world space.
    const Position& position() const;

    /// Update position (called by MovementProcessor).
    void set_position(const Position& pos);

    /// Mutable access to spell casting state (used by SpellCastProcessor).
    CastState& cast_state();

    /// Const access to spell casting state (used for inspection/telemetry).
    const CastState& cast_state() const;

private:
    uint64_t session_id_;
    Position position_;
    CastState cast_state_;
};

}  // namespace wow
