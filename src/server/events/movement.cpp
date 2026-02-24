#include "server/events/movement.h"

#include <string>
#include <unordered_set>

#include "server/telemetry/logger.h"

namespace wow {

// ---------------------------------------------------------------------------
// MovementEvent
// ---------------------------------------------------------------------------

MovementEvent::MovementEvent(uint64_t session_id, const Position& position)
    : GameEvent(EventType::MOVEMENT, session_id)
    , position_(position)
{
}

const Position& MovementEvent::position() const
{
    return position_;
}

// ---------------------------------------------------------------------------
// MovementProcessor
// ---------------------------------------------------------------------------

size_t MovementProcessor::process(
    std::vector<std::unique_ptr<GameEvent>>& events,
    std::unordered_map<uint64_t, Entity>& entities)
{
    std::unordered_set<uint64_t> updated_sessions;

    for (auto& event : events) {
        if (!event || event->event_type() != EventType::MOVEMENT) {
            continue;
        }

        auto* movement = static_cast<MovementEvent*>(event.get());
        uint64_t sid = movement->session_id();

        auto it = entities.find(sid);
        if (it == entities.end()) {
            // Unknown session — skip with warning.
            if (Logger::is_initialized()) {
                Logger::instance().error("movement", "Unknown session for movement event", {
                    {"session_id", sid},
                });
            }
            continue;
        }

        const Position& old_pos = it->second.position();
        const Position& new_pos = movement->position();

        it->second.set_position(new_pos);
        updated_sessions.insert(sid);

        if (Logger::is_initialized()) {
            Logger::instance().event("movement", "Position updated", {
                {"session_id", sid},
                {"old_x", old_pos.x},
                {"old_y", old_pos.y},
                {"old_z", old_pos.z},
                {"new_x", new_pos.x},
                {"new_y", new_pos.y},
                {"new_z", new_pos.z},
            });
        }
    }

    return updated_sessions.size();
}

}  // namespace wow
