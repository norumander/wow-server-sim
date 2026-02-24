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

private:
    uint64_t session_id_;
    Position position_;
};

}  // namespace wow
