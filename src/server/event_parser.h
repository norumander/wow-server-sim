#pragma once

#include <memory>
#include <nlohmann/json.hpp>

#include "server/events/event.h"

namespace wow {

/// Deserializes JSON objects into GameEvent instances.
///
/// Pure function class — no state, no I/O. Called by Connection on the
/// network thread after JSON line parsing. Invalid/unknown payloads
/// return nullptr (caller logs and drops).
class EventParser {
public:
    /// Parse a JSON object into a GameEvent. Returns nullptr on failure.
    static std::unique_ptr<GameEvent> parse(const nlohmann::json& obj);

private:
    static std::unique_ptr<GameEvent> parse_movement(
        const nlohmann::json& obj, uint64_t session_id);
    static std::unique_ptr<GameEvent> parse_spell_cast(
        const nlohmann::json& obj, uint64_t session_id);
    static std::unique_ptr<GameEvent> parse_combat(
        const nlohmann::json& obj, uint64_t session_id);
};

}  // namespace wow
