#include "server/event_parser.h"

namespace wow {

std::unique_ptr<GameEvent> EventParser::parse(const nlohmann::json& /*obj*/)
{
    // Stub — always returns nullptr. Tests should fail (RED step).
    return nullptr;
}

}  // namespace wow
