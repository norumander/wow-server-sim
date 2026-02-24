#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace wow {

/// Types of game events processed during the tick pipeline.
enum class EventType {
    MOVEMENT,    ///< Player position update
    SPELL_CAST,  ///< Spell cast initiation/completion (Step 7)
    COMBAT,      ///< Damage/healing application (Step 8)
};

/// Convert an EventType enum value to its string representation.
std::string_view event_type_to_string(EventType type);

/// Base class for all game events processed during the tick pipeline.
///
/// Owned via unique_ptr for single-ownership transfer through the event queue.
/// Derived types carry event-specific payloads (position, spell ID, etc.).
class GameEvent {
public:
    /// Construct a game event with a type tag and originating session ID.
    GameEvent(EventType type, uint64_t session_id);
    virtual ~GameEvent() = default;

    /// The event type tag (used for safe downcasting without RTTI).
    EventType event_type() const;

    /// The session that originated this event.
    uint64_t session_id() const;

    // Non-copyable (polymorphic type in unique_ptr).
    GameEvent(const GameEvent&) = delete;
    GameEvent& operator=(const GameEvent&) = delete;

    // Movable.
    GameEvent(GameEvent&&) = default;
    GameEvent& operator=(GameEvent&&) = default;

private:
    EventType type_;
    uint64_t session_id_;
};

}  // namespace wow
