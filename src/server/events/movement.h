#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "server/events/event.h"
#include "server/world/entity.h"

namespace wow {

/// Player position update event, processed during the MovementPhase.
///
/// Created by the network thread when a client sends a position update,
/// pushed into the EventQueue, and consumed by MovementProcessor.
class MovementEvent : public GameEvent {
public:
    /// Construct a movement event for the given session to the given position.
    MovementEvent(uint64_t session_id, const Position& position);

    /// The target position for this movement update.
    const Position& position() const;

private:
    Position position_;
};

/// Processes movement events during the MovementPhase of the tick pipeline.
///
/// Filters GameEvents for MOVEMENT type, updates entity positions, emits
/// telemetry. Events for unknown session_ids are skipped with a warning.
/// Multiple events for the same session in one tick: last one wins.
class MovementProcessor {
public:
    /// Process all movement events, updating entity positions.
    ///
    /// Iterates through the event vector, processes MOVEMENT events by updating
    /// the corresponding entity's position. Non-movement events are left
    /// untouched for later pipeline phases.
    ///
    /// @param events   All events for this tick (movement events are consumed).
    /// @param entities Map of session_id -> Entity for position updates.
    /// @return Number of entities whose positions were updated this tick.
    size_t process(std::vector<std::unique_ptr<GameEvent>>& events,
                   std::unordered_map<uint64_t, Entity>& entities);
};

}  // namespace wow
