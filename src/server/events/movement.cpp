#include "server/events/movement.h"

#include <string>

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
// MovementProcessor — stub (implemented in Commit 3)
// ---------------------------------------------------------------------------

size_t MovementProcessor::process(
    std::vector<std::unique_ptr<GameEvent>>& /*events*/,
    std::unordered_map<uint64_t, Entity>& /*entities*/)
{
    return 0;
}

}  // namespace wow
