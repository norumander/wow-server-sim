#include "server/events/event.h"

namespace wow {

// ---------------------------------------------------------------------------
// GameEvent
// ---------------------------------------------------------------------------

GameEvent::GameEvent(EventType type, uint64_t session_id)
    : type_(type)
    , session_id_(session_id)
{
}

EventType GameEvent::event_type() const
{
    return type_;
}

uint64_t GameEvent::session_id() const
{
    return session_id_;
}

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

std::string_view event_type_to_string(EventType type)
{
    switch (type) {
    case EventType::MOVEMENT:   return "MOVEMENT";
    case EventType::SPELL_CAST: return "SPELL_CAST";
    case EventType::COMBAT:     return "COMBAT";
    }
    return "UNKNOWN";
}

}  // namespace wow
