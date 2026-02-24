#include "server/world/entity.h"

#include <cmath>

namespace wow {

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------

bool operator==(const Position& a, const Position& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool operator!=(const Position& a, const Position& b)
{
    return !(a == b);
}

float distance(const Position& a, const Position& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// Entity
// ---------------------------------------------------------------------------

Entity::Entity(uint64_t session_id, EntityType type)
    : session_id_(session_id)
    , entity_type_(type)
    , position_()
{
}

uint64_t Entity::session_id() const
{
    return session_id_;
}

EntityType Entity::entity_type() const
{
    return entity_type_;
}

const Position& Entity::position() const
{
    return position_;
}

void Entity::set_position(const Position& pos)
{
    position_ = pos;
}

CastState& Entity::cast_state()
{
    return cast_state_;
}

const CastState& Entity::cast_state() const
{
    return cast_state_;
}

CombatState& Entity::combat_state()
{
    return combat_state_;
}

const CombatState& Entity::combat_state() const
{
    return combat_state_;
}

}  // namespace wow
