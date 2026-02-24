#include "server/session.h"

#include <array>
#include <string>

#include "server/telemetry/logger.h"

namespace wow {

// ---------------------------------------------------------------------------
// Transition table — 10 valid {from, event, to} entries.
// Linear scan is appropriate for this size (O(10) per transition).
// ---------------------------------------------------------------------------

static constexpr std::array<SessionTransition, 10> kTransitionTable{{
    {SessionState::CONNECTING,     SessionEvent::AUTHENTICATE_SUCCESS, SessionState::AUTHENTICATING},
    {SessionState::AUTHENTICATING, SessionEvent::ENTER_WORLD,          SessionState::IN_WORLD},
    {SessionState::IN_WORLD,       SessionEvent::DISCONNECT,           SessionState::DISCONNECTING},
    {SessionState::IN_WORLD,       SessionEvent::BEGIN_TRANSFER,       SessionState::TRANSFERRING},
    {SessionState::TRANSFERRING,   SessionEvent::TRANSFER_COMPLETE,    SessionState::IN_WORLD},
    {SessionState::TRANSFERRING,   SessionEvent::DISCONNECT,           SessionState::DISCONNECTING},
    {SessionState::DISCONNECTING,  SessionEvent::RECONNECT,            SessionState::AUTHENTICATING},
    {SessionState::DISCONNECTING,  SessionEvent::TIMEOUT,              SessionState::DESTROYED},
    {SessionState::CONNECTING,     SessionEvent::DISCONNECT,           SessionState::DESTROYED},
    {SessionState::AUTHENTICATING, SessionEvent::DISCONNECT,           SessionState::DISCONNECTING},
}};

// ---------------------------------------------------------------------------
// Static ID counter
// ---------------------------------------------------------------------------

std::atomic<uint64_t> Session::next_id_{1};

// ---------------------------------------------------------------------------
// Session implementation
// ---------------------------------------------------------------------------

Session::Session()
    : session_id_(next_id_.fetch_add(1))
    , state_(SessionState::CONNECTING)
{
}

uint64_t Session::session_id() const
{
    return session_id_;
}

SessionState Session::state() const
{
    return state_;
}

bool Session::transition(SessionEvent event)
{
    for (const auto& t : kTransitionTable) {
        if (t.from == state_ && t.event == event) {
            auto from = state_;
            state_ = t.to;

            if (Logger::is_initialized()) {
                Logger::instance().event("session", "State transition", {
                    {"session_id", session_id_},
                    {"from_state", std::string(session_state_to_string(from))},
                    {"to_state",   std::string(session_state_to_string(state_))},
                    {"event",      std::string(session_event_to_string(event))},
                });
            }
            return true;
        }
    }

    // No matching transition — invalid.
    if (Logger::is_initialized()) {
        Logger::instance().error("session", "Invalid state transition attempted", {
            {"session_id",    session_id_},
            {"current_state", std::string(session_state_to_string(state_))},
            {"event",         std::string(session_event_to_string(event))},
        });
    }
    return false;
}

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

std::string_view session_state_to_string(SessionState state)
{
    switch (state) {
    case SessionState::CONNECTING:     return "CONNECTING";
    case SessionState::AUTHENTICATING: return "AUTHENTICATING";
    case SessionState::IN_WORLD:       return "IN_WORLD";
    case SessionState::TRANSFERRING:   return "TRANSFERRING";
    case SessionState::DISCONNECTING:  return "DISCONNECTING";
    case SessionState::DESTROYED:      return "DESTROYED";
    }
    return "UNKNOWN";
}

std::string_view session_event_to_string(SessionEvent event)
{
    switch (event) {
    case SessionEvent::AUTHENTICATE_SUCCESS: return "AUTHENTICATE_SUCCESS";
    case SessionEvent::ENTER_WORLD:          return "ENTER_WORLD";
    case SessionEvent::DISCONNECT:           return "DISCONNECT";
    case SessionEvent::BEGIN_TRANSFER:       return "BEGIN_TRANSFER";
    case SessionEvent::TRANSFER_COMPLETE:    return "TRANSFER_COMPLETE";
    case SessionEvent::RECONNECT:            return "RECONNECT";
    case SessionEvent::TIMEOUT:              return "TIMEOUT";
    }
    return "UNKNOWN";
}

}  // namespace wow
